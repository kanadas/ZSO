#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");

int hello_init(void)
{
	printk(KERN_INFO "Hello world\n");
	return 0;
}

void hello_cleanup(void)
{
	printk(KERN_INFO "Goodbye cruel world\n");
}

module_init(hello_init);
module_exit(hello_cleanup);
