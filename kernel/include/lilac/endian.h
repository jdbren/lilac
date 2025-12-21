#ifndef __LILAC_ENDIAN_H__
#define __LILAC_ENDIAN_H__

#include <lilac/types.h>

typedef u16 __le16;
typedef u32 __le32;
typedef u64 __le64;

typedef u16 __be16;
typedef u32 __be32;
typedef u64 __be64;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

static inline u16 le16_to_cpu(__le16 x) { return x; }
static inline __le16 cpu_to_le16(u16 x) { return x; }

static inline u32 le32_to_cpu(__le32 x) { return x; }
static inline __le32 cpu_to_le32(u32 x) { return x; }

static inline u64 le64_to_cpu(__le64 x) { return x; }
static inline __le64 cpu_to_le64(u64 x) { return x; }

static inline u16 be16_to_cpu(__be16 x) { return __builtin_bswap16(x); }
static inline __be16 cpu_to_be16(u16 x) { return __builtin_bswap16(x); }

static inline u32 be32_to_cpu(__be32 x) { return __builtin_bswap32(x); }
static inline __be32 cpu_to_be32(u32 x) { return __builtin_bswap32(x); }

static inline u64 be64_to_cpu(__be64 x) { return __builtin_bswap64(x); }
static inline __be64 cpu_to_be64(u64 x) { return __builtin_bswap64(x); }

#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

static inline u16 le16_to_cpu(__le16 x) { return __builtin_bswap16(x); }
static inline __le16 cpu_to_le16(u16 x) { return __builtin_bswap16(x); }

static inline u32 le32_to_cpu(__le32 x) { return __builtin_bswap32(x); }
static inline __le32 cpu_to_le32(u32 x) { return __builtin_bswap32(x); }

static inline u64 le64_to_cpu(__le64 x) { return __builtin_bswap64(x); }
static inline __le64 cpu_to_le64(u64 x) { return __builtin_bswap64(x); }

static inline u16 be16_to_cpu(__be16 x) { return x; }
static inline __be16 cpu_to_be16(u16 x) { return x; }

static inline u32 be32_to_cpu(__be32 x) { return x; }
static inline __be32 cpu_to_be32(u32 x) { return x; }

static inline u64 be64_to_cpu(__be64 x) { return x; }
static inline __be64 cpu_to_be64(u64 x) { return x; }

#endif

#endif
