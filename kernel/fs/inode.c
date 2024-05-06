#include <lilac/fs.h>

#define L1_CACHE_BYTES 64

#define GOLDEN_RATIO_32 0x61C88647UL
#define GOLDEN_RATIO_64 0x61C8864680B583EBULL

#if BITS_PER_LONG == 32
#define GOLDEN_RATIO_NUM GOLDEN_RATIO_32
#elif BITS_PER_LONG == 64
#define GOLDEN_RATIO_NUM GOLDEN_RATIO_64
#endif

static const unsigned long i_hash_shift = 16;
static const unsigned long i_hash_mask = (1 << i_hash_shift) - 1;

static unsigned long hash(struct super_block *sb, unsigned long hashval)
{
	unsigned long tmp;

	tmp = (hashval * (unsigned long)sb) ^ (GOLDEN_RATIO_NUM + hashval) /
			L1_CACHE_BYTES;
	tmp = tmp ^ ((tmp ^ GOLDEN_RATIO_NUM) >> i_hash_shift);
	return tmp & i_hash_mask;
}

int insert_inode(struct super_block *sb, struct inode *inode)
{

	return 0;
}
