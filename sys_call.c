#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/syscalls.h>
 
long (*STUB_initialize_bar)(void) = NULL;
EXPORT_SYMBOL(STUB_initialize_bar);
 
SYSCALL_DEFINE0(initialize_bar) {
	if(initialize_bar != NULL)
		return STUB_initialize_bar();
	else
		return -ENOSYS;
}
 
long (*STUB_customer_arrival)(int, int) = NULL;
EXPORT_SYMBOL(STUB_customer_arrival, int, party, int, type);
 
SYSCALL_DEFINE2(STUB_customer_arrival) {
	if(STUB_customer_arrival != NULL)
		return STUB_customer_arrival();
	else
		return -ENOSYS;
}
 
long (*STUB_close_bar)(void) = NULL;
EXPORT_SYMBOL(STUB_close_bar);
 
SYSCALL_DEFINE0(STUB_close_bar) {
	if(STUB_close_bar != NULL)
		return STUB_close_bar();
	else
		return -ENOSYS;
}
