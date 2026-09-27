#!/bin/bash
# Build everything, create a throwaway test disk image (no sudo needed), boot
# it headless in qemu with /tests/run as init, and report the results.
#
# Each suite gets its own boot. If the kernel panics or hangs, the test that
# was running is recorded as PANIC/HANG and the suite is rebooted, resuming
# after that test.
#
# Environment:
#   KTEST_NO_BUILD=1       skip the make step
#   KTEST_SUITES="a b"     suites to run (default: all)
#   KTEST_BOOT_TIMEOUT=N   max seconds per boot (default 600)
#   KTEST_STALL=N          kill qemu if the log is silent for N seconds (default 30)
#   KTEST_RETRIES=N        max reboots per suite after a panic/hang (default 10)
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=build/test
IMG=$OUT/test.img
LOG=$OUT/debug.txt
REPORT=$OUT/report.txt
TEST_GUID=7E570000-0000-4000-8000-000000000001
SYSROOT=sysroot
ALL_SUITES="proc mem signal fs pipe thread time misc zz_hazard"
SUITES=${KTEST_SUITES:-$ALL_SUITES}
BOOT_TIMEOUT=${KTEST_BOOT_TIMEOUT:-600}
STALL=${KTEST_STALL:-30}
RETRIES=${KTEST_RETRIES:-10}

if [[ "${KTEST_NO_BUILD:-0}" != 1 ]]; then
    make -s -j"$(nproc)" install-system tests
fi

for f in "$SYSROOT/boot/lilac.ker" "$SYSROOT/tests/run" "$SYSROOT/lib/ld-lilac.so.1"; do
    [[ -e "$f" ]] || { echo "missing $f; build failed?" >&2; exit 2; }
done

mkdir -p "$OUT"
STAGE=$OUT/stage
rm -rf "$STAGE" "$IMG" "$OUT"/log-*
mkdir -p "$STAGE"

# ---- host-side manifests used by the fs suite to verify file contents ----
(cd "$SYSROOT" && cksum boot/lilac.ker lib/ld-lilac.so.1 bin/*) \
    | awk '{ print $1, $2, "/" $3 }' > "$OUT/root-manifest.txt"

: > "$OUT/ext2-manifest.txt"
if [[ -f ext2-test.img ]] && command -v debugfs >/dev/null; then
    mkdir -p "$STAGE/ext2"
    debugfs -R "rdump /bin /boot /lib /sbin $STAGE/ext2" "ext2-test.img?offset=1048576" \
        >/dev/null 2>&1 || true
    (cd "$STAGE/ext2" && find . -type f | sort | xargs -r cksum) \
        | awk '{ sub(/^\./, "", $3); print $1, $2, $3 }' > "$OUT/ext2-manifest.txt"
fi

# ---- build the disk image: GPT + FAT32 ESP, populated with mtools ----
truncate -s 512M "$IMG"
sfdisk -q "$IMG" <<EOF
label: gpt
start=2048, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, uuid=$TEST_GUID, name="ESP"
EOF
PART_SECTORS=$(sfdisk -d "$IMG" | awk -F'size=' '/start=/ { split($2, a, ","); print a[1] + 0 }')
mkfs.fat -F32 -s 8 -n LILACTEST --offset 2048 "$IMG" $((PART_SECTORS / 2)) >/dev/null 2>&1

export MTOOLS_SKIP_CHECK=1
M="$IMG@@1M"
mmd -i "$M" ::/EFI ::/EFI/BOOT ::/boot ::/boot/grub ::/ktest
mcopy -i "$M" resources/EFI/BOOT/BOOTIA32.EFI ::/EFI/BOOT/
mcopy -i "$M" resources/grub/grub-test.cfg ::/boot/grub/grub.cfg
for entry in "$SYSROOT"/*; do
    [[ "$(basename "$entry")" == boot ]] && continue
    mcopy -s -i "$M" "$entry" ::/
done
mcopy -i "$M" "$SYSROOT/boot/lilac.ker" ::/boot/
mcopy -o -i "$M" "$OUT/root-manifest.txt" "$OUT/ext2-manifest.txt" ::/tests/
# the test runner is init on this image
mcopy -o -i "$M" "$SYSROOT/tests/run" ::/sbin/init

# The fat32 tests leave files behind; start each boot from the same image.
cp --sparse=always "$IMG" "$OUT/pristine.img"

# ---- qemu ----
if [[ -r /dev/kvm && -w /dev/kvm ]]; then
    ACCEL=(-enable-kvm -cpu host,+tsc-deadline,+invtsc,+rdtscp,+vmware-cpuid-freq,+fsgsbase,+smap,+smep)
else
    echo "warning: /dev/kvm unavailable, using TCG (slow)" >&2
    ACCEL=(-cpu max)
fi
EXT2=()
[[ -f ext2-test.img ]] && EXT2=(-drive file=./ext2-test.img,media=disk,format=raw,snapshot=on)

# boot <log>; prints "ok", "panic" or "hang"
boot() {
    local log=$1
    rm -f "$log"
    qemu-system-x86_64 \
        "${ACCEL[@]}" -no-reboot -smp 4 -m 256M \
        -machine q35,firmware=./resources/OVMF-pure-efi.fd \
        -drive file="$IMG",format=raw \
        "${EXT2[@]}" \
        -net none -display none -serial none \
        -monitor unix:"$OUT/monitor.sock",server,nowait \
        -debugcon file:"$log" &
    local qpid=$! start=$SECONDS last_change=$SECONDS last_size=-1 size result=ok
    while kill -0 "$qpid" 2>/dev/null; do
        sleep 1
        size=$(stat -c %s "$log" 2>/dev/null || echo 0)
        if [[ "$size" != "$last_size" ]]; then
            last_size=$size
            last_change=$SECONDS
        fi
        if grep -aq ' PANIC ' "$log" 2>/dev/null; then
            sleep 2  # let the backtrace finish
            result=panic
            break
        fi
        if (( SECONDS - last_change > STALL || SECONDS - start > BOOT_TIMEOUT )); then
            result=hang
            break
        fi
    done
    if [[ $result == hang ]] && kill -0 "$qpid" 2>/dev/null; then
        python3 scripts/ktest-hangdump.py "$OUT/monitor.sock" kernel/lilac.ker \
            > "$log.hang" 2>&1 || true
    fi
    kill -9 "$qpid" 2>/dev/null || true
    wait "$qpid" 2>/dev/null || true
    echo "$result"
}

: > "$LOG"
for suite in $SUITES; do
    args=""
    for ((attempt = 1; attempt <= RETRIES + 1; attempt++)); do
        cp --sparse=always "$OUT/pristine.img" "$IMG"
        echo "$suite $args" > "$STAGE/plan"
        mcopy -o -i "$M" "$STAGE/plan" ::/tests/plan
        log="$OUT/log-$suite-$attempt.txt"
        printf '%-10s boot %d%s ... ' "$suite" "$attempt" "${args:+ ($args)}"
        result=$(boot "$log")
        tr -d '\r' < "$log" >> "$LOG"
        if grep -aq "^KTEST SUITE $suite END" "$log"; then
            echo "done"
            break
        fi
        # Attribute the crash to the last test that started.
        culprit=$(grep -a "^KTEST $suite\.[^ ]* START" "$log" | tail -1 | awk '{ print $2 }')
        status=$([[ $result == panic ]] && echo PANIC || echo HANG)
        echo "$status${culprit:+ in $culprit}"
        if [[ -z "$culprit" ]]; then
            echo "KTEST SUITE $suite ABORTED $status before any test started" >> "$LOG"
            break
        fi
        echo "KTEST $culprit $status" >> "$LOG"
        if [[ $result == panic ]]; then
            grep -a -A14 ' PANIC ' "$log" | head -15 | tr -d '\r' \
                | sed "s/^/KTEST   $culprit: /" >> "$LOG"
        else
            echo "KTEST   $culprit: no kernel output for ${STALL}s (last: $(grep -a . "$log" | tail -1 | tr -d '\r' | cut -c1-150))" >> "$LOG"
            [[ -f "$log.hang" ]] && sed "s/^/KTEST   $culprit: /" "$log.hang" >> "$LOG"
        fi
        args="-s ${culprit#"$suite".}"
        if (( attempt == RETRIES + 1 )); then
            echo "KTEST SUITE $suite ABORTED too many crashes" >> "$LOG"
        fi
    done
done

# ---- report ----
python3 - "$LOG" "$REPORT" "$SUITES" <<'PY'
import re, sys
log_path, report_path, suites = sys.argv[1], sys.argv[2], sys.argv[3].split()
lines = open(log_path, 'rb').read().decode('utf-8', 'replace').split('\n')

res_re = re.compile(r'^KTEST (\S+) (PASS|FAIL|SKIP|CRASH|TIMEOUT|PANIC|HANG)\b(.*)$')
detail_re = re.compile(r'^KTEST   (\S+): (.*)$')
results, details, order = {}, {}, []
for ln in lines:
    m = res_re.match(ln)
    if m:
        name, status, rest = m.groups()
        if name not in results:
            order.append(name)
        results[name] = (status, rest.strip())
        continue
    m = detail_re.match(ln)
    if m:
        details.setdefault(m.group(1), []).append(m.group(2))

BAD = ('FAIL', 'CRASH', 'TIMEOUT', 'PANIC', 'HANG')
counts = {}
for n in order:
    counts[results[n][0]] = counts.get(results[n][0], 0) + 1

out = []
bad = [n for n in order if results[n][0] in BAD]
if bad:
    out.append('Failures:')
    for n in bad:
        status, rest = results[n]
        out.append(f'  {status:7} {n} {rest}'.rstrip())
        for d in details.get(n, []):
            if not d.startswith('note:'):
                out.append(f'            {d}')
    out.append('')
skips = [n for n in order if results[n][0] == 'SKIP']
if skips:
    out.append('Skipped: ' + ', '.join(skips))
    out.append('')
problems = [l[len('KTEST SUITE '):] for l in lines
            if re.match(r'^KTEST SUITE \S+ (TIMEOUT|MISSING|FORKFAIL|ABORTED|END signal)', l)]
ended = {l.split()[2] for l in lines if re.match(r'^KTEST SUITE \S+ END', l)}
for s in suites:
    if s not in ended:
        problems.append(f'{s} did not run to completion')
for p in problems:
    out.append('Suite problem: ' + p)
if problems:
    out.append('')
out.append('Summary: %d tests: %s' % (len(order), ', '.join(
    f'{counts.get(k, 0)} {k.lower()}' for k in ('PASS',) + BAD + ('SKIP',))))
text = '\n'.join(out) + '\n'
open(report_path, 'w').write(text)
sys.stdout.write(text)
sys.exit(0 if not bad and not problems else 1)
PY
