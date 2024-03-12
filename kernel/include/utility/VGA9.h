#include <kernel/types.h>
static const char VGA9[] = "\
SFN2{\026\000\000\003\000\b\020\020\020.\000\006\n\
\000\000\000\000\000\000\000\000\000\000\000\000\000\000VGA9.F16\000\000\000\000\000\000\200\001\030\030\200\000\377\200\000~\200\006\0360>333n\200\000c\200\006>c\177\003\003c>\200\006\034\030\030\030\030\030<\200\006>ccccc>\200\006333333n\200\001n;\200\002\006\f\030\200\000\177\200\002\030\f\006\200\002\b\0346\200\004\030\030~\030\030\200\006;ffffff\200\003\03466\034\200\002\0346\034\200\0003\200\0020\030\f\200\b\377\030\030\030\030\030\030\030\030\200\b\357llllllll\200\005lllll\357\200\006\030<<<\030\030\030\200\003fff\044\200\b66\177666\17766\200\r\030\030>cC\003>``ac>\030\030\200\aCc0\030\f\006ca\200\t\03466\034n;333n\200\003\f\f\f\006\200\t0\030\f\f\f\f\f\f\0300\200\t\f\030000000\030\f\200\004f<\377<f\200\003\030\030\030\f\200\a\100`0\030\f\006\003\001\200\t<f\303\303\333\333\303\303f<\200\t\030\034\036\030\030\030\030\030\030~\200\t>c`0\030\f\006\003c\177\200\t>c``<```c>\200\t08<63\177000x\200\t\177\003\003\003\077```c>\200\t\034\006\003\003\077cccc>\200\t\177c``0\030\f\f\f\f\200\t>ccc>cccc>\200\t>ccc~```0\036\200\002\030\030\f\200\b`0\030\f\006\f\0300`\200\b\006\f\0300`0\030\f\006\200\006>cc0\030\030\030\200\b>cc{{{;\003>\200\t\b\0346cc\177cccc\200\t\077fff>ffff\077\200\t<fC\003\003\003\003Cf<\200\t\0376ffffff6\037\200\t\177fF\026\036\026\006Ff\177\200\t\177fF\026\036\026\006\006\006\017\200\t<fC\003\003{ccf\\\200\tcccc\177ccccc\200\t<\030\030\030\030\030\030\030\030<\200\tx00000333\036\200\tgff6\036\0366ffg\200\t\017\006\006\006\006\006\006Ff\177\200\t\303\347\377\377\333\303\303\303\303\303\200\tcgo\177{scccc\200\t>cccccccc>\200\t\077fff>\006\006\006\006\017\200\v>cccccck{>0p\200\t\077fff>6fffg\200\t>cc\006\0340`cc>\200\t\377\333\231\030\030\030\030\030\030<\200\tccccccccc>\200\t\303\303\303\303\303\303\303f<\030\200\t\303\303\303\303\303\333\333\377ff\200\t\303\303f<\030\030<f\303\303\200\t\303\303\303f<\030\030\030\030<\200\t\377\303a0\030\f\006\203\303\377\200\t<\f\f\f\f\f\f\f\f<\200\b\001\003\a\016\0348p`\100\200\t<00000000<\200\003\b\0346c\200\002\f\f\030\200\t\a\006\006\0366ffff>\200\006>c\003\003\003c>\200\t800<63333n\200\t\0346&\006\017\006\006\006\006\017\200\tn33333>03\036\200\t\a\006\0066nffffg\200\001``\200\tp``````ff<\200\t\a\006\006f6\036\0366fg\200\t\034\030\030\030\030\030\030\030\030<\200\006g\377\333\333\333\333\333\200\t;fffff>\006\006\017\200\tn33333>00x\200\006;nf\006\006\006\017\200\006>c\006\0340c>\200\t\b\f\f\077\f\f\f\fl8\200\006\303\303\303\303f<\030\200\006\303\303\303\333\333\377f\200\006\303f<\030<f\303\200\tcccccc~`0\037\200\006\1773\030\f\006c\177\200\tp\030\030\030\016\030\030\030\030p\200\003\030\030\030\030\200\004\030\030\030\030\030\200\t\016\030\030\030p\030\030\030\030\016\200\006\b\0346ccc\177\200\006\030\030\030<<<\030\200\n\
\030\030~\303\003\003\003\303~\030\030\200\n\
\0346&\006\017\006\006\006\006g\077\200\t\303f<\030\377\030\377\030\030\030\200\v>c\006\0346cc6\0340c>\200\003<66|\200\004l6\0336l\200\004\177````\200\005\016\033\f\006\023\037\200\bfffff>\006\006\003\200\t\376\333\333\333\336\330\330\330\330\330\200\000\030\200\000>\200\004\0336l6\033\200\f\003\003Cc3\030\ffsi|``\200\f\003\003Cc3\030\f\006s\331`0\370\200\001\f\f\200\006\f\f\006\003cc>\200\b\b\0346cc\177ccc\200\a\0346cc\177ccc\200\t|633\1773333s\200\v<fC\003\003\003Cf<0`>\200\a\177f\006>\006\006f\177\200\bcgo\177{sccc\200\b>ccccccc>\200\bcccccccc>\200\t\036333\0333ccc3\200\006v\334\330~\033;\356\200\b<f\006\006f<0`<\200\002\030<f\200\000f\200\002\f\0363\200\tcccccc~`0\036\200\fp\330\030\030\030~\030\030\030\030\030\033\016\200\t\177cc\003\003\003\003\003\003\003\200\b\0346cc\177cc6\034\200\b\177c\006\f\030\f\006c\177\200\b~\030<fff<\030~\200\t\0346ccc6666w\200\006n;\033\033\033;n\200\tx\f\0300|ffff<\200\t8\f\006\006>\006\006\006\f8\200\a\1776666666\200\006~\033\033\033\033\033\016\200\an;\030\030\030\030\030\030\200\b\300`~\333\333\317~\006\003\200\003\030<<\030\200\006fffffff\200\001ff\200\005\03366666\200\n\
\077ff>Ff\366fff\317\200\004\f\006\177\006\f\200\t\030<~\030\030\030\030\030\030\030\200\004\0300\1770\030\200\t\030\030\030\030\030\030\030~<\030\200\004\044f\377f\044\200\b\030<~\030\030\030~<\030\200\t\030<~\030\030\030~<\030~\200\n\
\36000000766<8\200\004~\333\333\333~\200\003\003\003\003\177\200\b>cccccccc\200\0060\030\f\006\f\0300\200\006\f\0300`0\030\f\200\004\177\003\003\003\003\200\rp\330\330\030\030\030\030\030\030\030\030\030\030\030\200\v\030\030\030\030\030\030\030\030\033\033\033\016\200\017\030\030\030\030\030\030\030\030\030\030\030\030\030\030\030\030\200\b\370\030\030\030\030\030\030\030\030\200\b\037\030\030\030\030\030\030\030\030\200\a\030\030\030\030\030\030\030\370\200\a\030\030\030\030\030\030\030\037\200\017\030\030\030\030\030\030\030\370\030\030\030\030\030\030\030\030\200\017\030\030\030\030\030\030\030\037\030\030\030\030\030\030\030\030\200\a\030\030\030\030\030\030\030\377\200\017\030\030\030\030\030\030\030\377\030\030\030\030\030\030\030\030\200\017llllllllllllllll\200\n\
\370\030\370\030\030\030\030\030\030\030\030\200\b\374llllllll\200\n\
\374\f\354llllllll\200\n\
\037\030\037\030\030\030\030\030\030\030\030\200\b\177llllllll\200\n\
\177`ollllllll\200\a\030\030\030\030\030\370\030\370\200\alllllll\374\200\alllll\354\f\374\200\a\030\030\030\030\030\037\030\037\200\alllllll\177\200\alllllo`\177\200\017\030\030\030\030\030\370\030\370\030\030\030\030\030\030\030\030\200\017lllllll\354llllllll\200\017lllll\354\f\354llllllll\200\017\030\030\030\030\030\037\030\037\030\030\030\030\030\030\030\030\200\017lllllllollllllll\200\017lllllo`ollllllll\200\b\377llllllll\200\005\030\030\030\030\030\377\200\alllllll\377\200\017\030\030\030\030\030\377\030\377\030\030\030\030\030\030\030\030\200\017lllllll\377llllllll\200\006\377\377\377\377\377\377\377\200\b\377\377\377\377\377\377\377\377\377\200\017\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377\200\017\017\017\017\017\017\017\017\017\017\017\017\017\017\017\017\017\200\017\360\360\360\360\360\360\360\360\360\360\360\360\360\360\360\360\200\017\210\"\210\"\210\"\210\"\210\"\210\"\210\"\210\"\200\017\252U\252U\252U\252U\252U\252U\252U\252U\200\017\273\356\273\356\273\356\273\356\273\356\273\356\273\356\273\356\200\006>>>>>>>\200\003\177\177\177\177\200\006\b\034\034>>\177\177\200\n\
\001\003\a\017\037\177\037\017\a\003\001\200\006\177\177>>\034\034\b\200\n\
\100`px|\177|xp`\100\200\005<fBBf<\200\017\377\377\377\377\377\377\347\303\303\347\377\377\377\377\377\377\200\017\377\377\377\377\377\303\231\275\275\231\303\377\377\377\377\377\200\t~\201\245\201\201\275\231\201\201~\200\t~\377\333\377\377\303\347\377\377~\200\b\030\030\333<\347<\333\030\030\200\t<ffff<\030~\030\030\200\txpXL\0363333\036\200\b\030<~\377\377~\030\030<\200\b\030<<\347\347\347\030\030<\200\a6\177\177\177\177>\034\b\200\006\b\034>\177>\034\b\200\t\374\314\374\f\f\f\f\016\017\a\200\n\
\376\306\376\306\306\306\306\346\347g\003\237\000\000\b\020\t\000\000\002\b\020\t\000\000\002\277\000\000\000\n\
.\000\000\000\001\b\020\t\000\000\001\310\000\000\000\001\b\020\t\000\000\003\316\000\000\000\001\b\020\t\000\000\000\331\000\000\000\001\b\020\t\000\000\004\351\000\000\000\001\b\020\t\000\000\002\363\000\000\000\001\b\020\t\000\000\001\377\000\000\000\001\b\020\t\000\000\002\005\001\000\000\001\b\020\t\000\000\002\021\001\000\000\001\b\020\t\000\000\005\035\001\000\000\001\b\020\t\000\000\005~\000\000\000\001\b\020\t\000\000\t\044\001\000\000\001\b\020\t\000\000\aq\000\000\000\001\b\020\t\000\000\n\
.\000\000\000\001\b\020\t\000\000\004*\001\000\000\001\b\020\t\000\000\0024\001\000\000\001\b\020\t\000\000\002\100\001\000\000\001\b\020\t\000\000\002L\001\000\000\001\b\020\t\000\000\002X\001\000\000\001\b\020\t\000\000\002d\001\000\000\001\b\020\t\000\000\002p\001\000\000\001\b\020\t\000\000\002|\001\000\000\001\b\020\t\000\000\002\210\001\000\000\001\b\020\t\000\000\002\224\001\000\000\001\b\020\t\000\000\002\240\001\000\000\002\b\020\t\000\000\004.\000\000\000\t.\000\000\000\002\b\020\t\000\000\004.\000\000\000\t\254\001\000\000\001\b\020\t\000\000\003\261\001\000\000\002\b\020\t\000\000\0055\000\000\000\b5\000\000\000\001\b\020\t\000\000\003\274\001\000\000\002\b\020\t\000\000\002\307\001\000\000\n\
.\000\000\000\001\b\020\t\000\000\003\320\001\000\000\001\b\020\t\000\000\002\333\001\000\000\001\b\020\t\000\000\002\347\001\000\000\001\b\020\t\000\000\002\363\001\000\000\001\b\020\t\000\000\002\377\001\000\000\001\b\020\t\000\000\002\v\002\000\000\001\b\020\t\000\000\002\027\002\000\000\001\b\020\t\000\000\002#\002\000\000\001\b\020\t\000\000\002/\002\000\000\001\b\020\t\000\000\002;\002\000\000\001\b\020\t\000\000\002G\002\000\000\001\b\020\t\000\000\002S\002\000\000\001\b\020\t\000\000\002_\002\000\000\001\b\020\t\000\000\002k\002\000\000\001\b\020\t\000\000\002w\002\000\000\001\b\020\t\000\000\002\203\002\000\000\001\b\020\t\000\000\002\217\002\000\000\001\b\020\t\000\000\002\233\002\000\000\001\b\020\t\000\000\002\251\002\000\000\001\b\020\t\000\000\002\265\002\000\000\001\b\020\t\000\000\002\301\002\000\000\001\b\020\t\000\000\002\315\002\000\000\001\b\020\t\000\000\002\331\002\000\000\001\b\020\t\000\000\002\345\002\000\000\001\b\020\t\000\000\002\361\002\000\000\001\b\020\t\000\000\002\375\002\000\000\001\b\020\t\000\000\002\t\003\000\000\001\b\020\t\000\000\002\025\003\000\000\001\b\020\t\000\000\003!\003\000\000\001\b\020\t\000\000\002,\003\000\000\001\b\020\t\000\000\0008\003\000\000\001\b\020\t\000\000\r2\000\000\000\001\b\020\t\000\000\000>\003\000\000\001\b\020\t\000\000\0058\000\000\000\001\b\020\t\000\000\002C\003\000\000\001\b\020\t\000\000\005O\003\000\000\001\b\020\t\000\000\002X\003\000\000\001\b\020\t\000\000\005D\000\000\000\001\b\020\t\000\000\002d\003\000\000\001\b\020\t\000\000\005p\003\000\000\001\b\020\t\000\000\002|\003\000\000\002\b\020\t\000\000\002.\000\000\000\005M\000\000\000\002\b\020\t\000\000\002\210\003\000\000\005\214\003\000\000\001\b\020\t\000\000\002\230\003\000\000\001\b\020\t\000\000\002\244\003\000\000\001\b\020\t\000\000\005\260\003\000\000\001\b\020\t\000\000\005\205\000\000\000\001\b\020\t\000\000\005V\000\000\000\001\b\020\t\000\000\005\271\003\000\000\001\b\020\t\000\000\005\305\003\000\000\001\b\020\t\000\000\005\321\003\000\000\001\b\020\t\000\000\005\332\003\000\000\001\b\020\t\000\000\002\343\003\000\000\001\b\020\t\000\000\005_\000\000\000\001\b\020\t\000\000\005\357\003\000\000\001\b\020\t\000\000\005\370\003\000\000\001\b\020\t\000\000\005\001\004\000\000\001\b\020\t\000\000\005\n\
\004\000\000\001\b\020\t\000\000\005\026\004\000\000\001\b\020\t\000\000\002\037\004\000\000\002\b\020\t\000\000\002+\004\000\000\a1\004\000\000\001\b\020\t\000\000\0028\004\000\000\001\b\020\t\000\000\002h\000\000\000\001\b\020\t\000\000\004D\004\000\237\000\000\b\020\t\000\000\002\b\020\t\000\000\002.\000\000\000\005M\004\000\000\001\b\020\t\000\000\001V\004\000\000\001\b\020\t\000\000\001c\004\000\200\000\001\b\020\t\000\000\002p\004\000\200\000\001\b\020\t\000\000\001|\004\000\201\000\002\b\020\t\000\000\001\212\004\000\000\0065\000\000\000\001\b\020\t\000\000\005\220\004\000\000\001\b\020\t\000\000\006\227\004\000\202\000\001\b\020\t\000\000\001\216\000\000\000\002\b\020\t\000\000\004~\000\000\000\v2\000\000\000\001\b\020\t\000\000\001\236\004\000\201\000\001\b\020\t\000\000\004\246\004\000\000\001\b\020\t\000\000\002\261\004\000\000\001\b\020\t\000\000\b\275\004\000\201\000\002\b\020\t\000\000\001\216\000\000\000\006\300\004\000\000\001\b\020\t\000\000\005\303\004\000\000\001\b\020\t\000\000\001\312\004\000\000\001\b\020\t\000\000\001\331\004\000\200\000\002\b\020\t\000\000\002\350\004\000\000\005\354\004\000\203\000\002\b\020\t\000\000\001A\000\000\000\003\365\004\000\000\002\b\020\t\000\000\000\224\000\000\000\004\000\005\000\000\001\b\020\t\000\000\002\n\
\005\000\000\001\b\020\t\000\000\002\026\005\000\200\000\002\b\020\t\000\000\000t\000\000\000\004\044\005\000\206\000\002\b\020\t\000\000\000h\000\000\000\003.\005\000\203\000\002\b\020\t\000\000\001A\000\000\000\0039\005\000\204\000\002\b\020\t\000\000\001A\000\000\000\003D\005\000\201\000\001\b\020\t\000\000\002O\005\000\000\002\b\020\t\000\000\001l\000\000\000\0058\000\000\000\002\b\020\t\000\000\001t\000\000\000\0058\000\000\000\002\b\020\t\000\000\001y\000\000\000\0058\000\000\200\000\002\b\020\t\000\000\002\231\000\000\000\0058\000\000\000\002\b\020\t\000\000\001\224\000\000\000\0058\000\000\000\001\b\020\t\000\000\005[\005\000\000\001\b\020\t\000\000\004d\005\000\000\002\b\020\t\000\000\001l\000\000\000\005D\000\000\000\002\b\020\t\000\000\001\234\000\000\000\005D\000\000\000\002\b\020\t\000\000\001y\000\000\000\005D\000\000\000\002\b\020\t\000\000\002A\000\000\000\005D\000\000\000\002\b\020\t\000\000\001l\000\000\000\005M\000\000\000\002\b\020\t\000\000\001\234\000\000\000\005M\000\000\000\002\b\020\t\000\000\001o\005\000\000\005M\000\000\000\002\b\020\t\000\000\002t\005\000\000\005M\000\000\200\000\002\b\020\t\000\000\002h\000\000\000\005\205\000\000\000\002\b\020\t\000\000\001l\000\000\000\005V\000\000\000\002\b\020\t\000\000\001t\000\000\000\005V\000\000\000\002\b\020\t\000\000\001y\000\000\000\005V\000\000\200\000\002\b\020\t\000\000\002A\000\000\000\005V\000\000\000\003\b\020\t\000\000\004.\000\000\000\a5\000\000\000\t.\000\000\200\000\002\b\020\t\000\000\001l\000\000\000\005_\000\000\000\002\b\020\t\000\000\001t\000\000\000\005_\000\000\000\002\b\020\t\000\000\001w\005\000\000\005_\000\000\000\002\b\020\t\000\000\002\231\000\000\000\005_\000\000\201\000\002\b\020\t\000\000\002A\000\000\000\005|\005\000\300\221\000\001\b\020\t\000\000\001\210\005\000\301\377\000\001\b\020\t\000\000\002\227\005\000\203\000\001\b\020\t\000\000\003\243\005\000\211\000\001\b\020\t\000\000\003\256\005\000\201\000\001\b\020\t\000\000\003\271\005\000\201\000\001\b\020\t\000\000\002\304\005\000\206\000\001\b\020\t\000\000\005\320\005\000\201\000\001\b\020\t\000\000\002\331\005\000\000\001\b\020\t\000\000\002\345\005\000\211\000\001\b\020\t\000\000\004\361\005\000\201\000\001\b\020\t\000\000\005\373\005\000\000\001\b\020\t\000\000\004\004\006\000\200\000\001\b\020\t\000\000\003\016\006\000\334Z\000\001\b\020\t\000\000\006\031\006\000\230\000\002\b\020\t\000\000\002\037\006\000\000\n\
(\006\000\300A\000\001\b\020\t\000\000\001,\006\000\246\000\001\b\020\t\000\000\0014\006\000\300\347\000\001\b\020\t\000\000\005A\006\000\000\001\b\020\t\000\000\002H\006\000\000\001\b\020\t\000\000\005T\006\000\000\001\b\020\t\000\000\002[\006\000\000\001\b\020\t\000\000\005g\006\000\000\001\b\020\t\000\000\002n\006\000\221\000\001\b\020\t\000\000\002y\006\000\300o\000\001\b\020\t\000\000\a.\000\000\000\001\b\020\t\000\000\001\205\006\000\202\000\001\b\020\t\000\000\005\222\006\000\000\001\b\020\t\000\000\006\231\006\000\210\000\001\b\020\t\000\000\003\237\006\000\235\000\002\b\020\t\000\000\005h\000\000\000\bh\000\000\227\000\003\b\020\t\000\000\004q\000\000\000\aq\000\000\000\n\
q\000\000\201\000\002\b\020\t\000\000\003\252\006\000\000\v5\000\000\000\002\b\020\t\000\000\003\263\006\000\000\v5\000\000\300\251\000\001\b\020\t\000\000\006\274\006\000\216\000\001\b\020\t\000\000\002\303\006\000\000\001\b\020\t\000\000\000\323\006\000\301\335\000\001\b\020\t\000\000\a2\000\000\200\000\001\b\020\t\000\000\000\341\006\000\210\000\001\b\020\t\000\000\a\363\006\000\202\000\001\b\020\t\000\000\a\376\006\000\202\000\001\b\020\t\000\000\000\t\a\000\202\000\001\b\020\t\000\000\000\023\a\000\202\000\001\b\020\t\000\000\000\035\a\000\206\000\001\b\020\t\000\000\000/\a\000\206\000\001\b\020\t\000\000\a\241\000\000\206\000\001\b\020\t\000\000\000A\a\000\206\000\001\b\020\t\000\000\000K\a\000\222\000\002\b\020\t\000\000\0052\000\000\000\a2\000\000\000\001\b\020\t\000\000\000]\a\000\000\001\b\020\t\000\000\005o\a\000\000\001\b\020\t\000\000\a|\a\000\000\001\b\020\t\000\000\005\207\a\000\000\001\b\020\t\000\000\005\224\a\000\000\001\b\020\t\000\000\a\241\a\000\000\001\b\020\t\000\000\005\254\a\000\000\001\b\020\t\000\000\000\271\a\000\000\001\b\020\t\000\000\000\303\a\000\000\001\b\020\t\000\000\000\315\a\000\000\001\b\020\t\000\000\000\327\a\000\000\001\b\020\t\000\000\000\341\a\000\000\001\b\020\t\000\000\000\353\a\000\000\001\b\020\t\000\000\000\365\a\000\000\001\b\020\t\000\000\000\a\b\000\000\001\b\020\t\000\000\000\031\b\000\000\001\b\020\t\000\000\000+\b\000\000\001\b\020\t\000\000\000=\b\000\000\001\b\020\t\000\000\000O\b\000\000\002\b\020\t\000\000\0052\000\000\000\a\241\000\000\000\001\b\020\t\000\000\aa\b\000\000\002\b\020\t\000\000\0052\000\000\000\a\254\000\000\000\002\b\020\t\000\000\000l\b\000\000\a2\000\000\000\001\b\020\t\000\000\000t\b\000\000\002\b\020\t\000\000\000\267\000\000\000\a2\000\000\000\001\b\020\t\000\000\000~\b\000\000\001\b\020\t\000\000\000\220\b\000\000\002\b\020\t\000\000\000\267\000\000\000\a\254\000\000\222\000\001\b\020\t\000\000\000\242\b\000\202\000\001\b\020\t\000\000\a\253\b\000\202\000\001\b\020\t\000\000\000\266\b\000\202\000\001\b\020\t\000\000\000\310\b\000\202\000\001\b\020\t\000\000\000\332\b\000\000\001\b\020\t\000\000\000\354\b\000\000\001\b\020\t\000\000\000\376\b\000\000\001\b\020\t\000\000\000\020\t\000\213\000\001\b\020\t\000\000\004\"\t\000\212\000\001\b\020\t\000\000\b+\t\000\204\000\001\b\020\t\000\000\0041\t\000\206\000\001\b\020\t\000\000\001:\t\000\200\000\001\b\020\t\000\000\004G\t\000\206\000\001\b\020\t\000\000\001P\t\000\205\000\001\b\020\t\000\000\005]\t\000\213\000\001\b\020\t\000\000\000e\t\000\000\001\b\020\t\000\000\000w\t\000\300_\000\001\b\020\t\000\000\002\211\t\000\000\001\b\020\t\000\000\002\225\t\000\000\001\b\020\t\000\000\003\241\t\000\202\000\001\b\020\t\000\000\002\254\t\000\200\000\001\b\020\t\000\000\002\270\t\000\234\000\001\b\020\t\000\000\003\304\t\000\201\000\001\b\020\t\000\000\003\317\t\000\200\000\001\b\020\t\000\000\004\332\t\000\000\001\b\020\t\000\000\004\344\t\000\202\000\001\b\020\t\000\000\002\355\t\000\000\001\b\020\t\000\000\002\371\t\000\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377\376\377\376\377\376\377\334\2242NFS";
const size_t VGA9_len = sizeof(VGA9) - 1;
