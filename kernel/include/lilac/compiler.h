#ifndef _LILAC_COMPILER_H
#define _LILAC_COMPILER_H

#define typeof_member(T, m)	typeof(((T*)0)->m)

/* Are two types/vars the same type (ignoring qualifiers)? */
#ifndef __same_type
# define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#endif

#define static_assert(expr, msg) _Static_assert(expr, msg)

#define __BUILD_BUG_ON_ZERO_MSG(e, msg, ...) ((int)sizeof(struct {static_assert(!(e), msg);}))

#define __is_array(a)		(!__same_type((a), &(a)[0]))
#define __must_be_array(a)	__BUILD_BUG_ON_ZERO_MSG(!__is_array(a), \
							"must be array")



#endif /* _LILAC_COMPILER_H */
