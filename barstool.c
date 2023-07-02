/*
* Kernel Module Programming 
* COP4610 Project 2
* Stanley Vossler, Carlos Pantoja-Malaga, Matthew Echenique
*/
/*
* syscall
*/
#include <linux/module.h>
#include <linux/linkage.h>
/*
* data management
*/
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/list.h>
/*
* proc
*/
#include <linux/proc_fs.h>
/*
* threads
*/
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/mutex.h>
/*
* time
*/
#include <linux/time64.h>

MODULE_LICENSE("Dual BSD/GPL");

/*
* data structs and members 
*/

typedef struct {
	char type;
	int group_id;
	long time;
} guest;

typedef struct {
	char type;
	int group_id;
	int amt;
	struct list_head list;
} group;

typedef struct {
	guest g;
	char status;
} seat;

typedef struct {
	seat seats[8];
} table;

typedef struct {
	int table;
	char status;
} waiter;

typedef struct {
	table tables[4];
	int occupancy;
	int serviced;
	int waiting;
	int waiting_groups;
	waiter wtr;
	struct list_head queue;
	int open;
} bar;

static bar majorsbar = {
	.wtr.status = 'O',
	.open = 0,
};

struct mutex lock;

/*
* proc vars
*/
static struct proc_dir_entry* proc_entry;
static bool read;
static char* str;

/*
* time vars
*/
static bool initial_time_saved = false;
static struct timespec64 initial_time;
static struct task_struct *thread;

/*
* module functions
*/
static void setup(void);
static int add_group(int p, int t);
static int print_groups(void);

/*
* waiter functions
*/
static int simulate_waiter(void *pv);
static int to_do(void);

static int is_clean(int pos);
static int is_occupied(int pos);

static int load_table(int pos);
static int clean_table(int pos);
static int shrink_table(int pos);
static int remove_guest(char guest_type, long here, long now);

/*
*	sys call internals
*/

// initialize_bar definition -> what to do when open_bar syscall comes through
extern long(*STUB_initialize_bar)(void);
long open_bar(void) {	
	// check if bar is already intialized
	if(majorsbar.wtr.status != 'O') {
		printk(KERN_ALERT "Bar is already intialized dummy!");
		return  1;
	}
	
	// setup bar
	printk(KERN_ALERT "Initializing bar...");
	setup();
	
	// set time
	struct timespec64 current_time;
	ktime_get_real_ts64(&current_time);
        initial_time = current_time;
	
	// start waiter thread
	if(thread) {
		wake_up_process(thread);
	}
	
	return 0;
}

// customer_arrival defintion -> what to do when customer_arrival syscall comes through
extern long(*STUB_customer_arrival)(int, int);
long arrival(int party, int type) {
	printk(KERN_ALERT "Party of %d of type %d arrived.", party, type);
	
	// check to see if parameters are valid
	if(party <= 0 || party > 8)
		return 1;
	else if(type < 0 || type > 4)
		return 1;
	
	// if bar is open add group
	if(majorsbar.open == 1)
		return add_group(party, type);
	return 1;
}

// close_bar definition -> what to do when close_bar syscall comes throuhg
extern long(*STUB_close_bar)(void);
long shutdown_bar(void) {
	printk(KERN_ALERT "Shutting down bar...");
	
	// set bar to close -> do not take more customer_arrival requests
	majorsbar.open = 0;
	return 0;
}

/*
* proprietary functions
*/

// setup bar function
static void setup(void) {
	// set all seats to clean and available
	int i = 0, j = 0;
	for(i = 0; i < 4; i++)
		for(j = 0; j < 8; j++)
			majorsbar.tables[i].seats[j].status = 'C';
	
	// set bar to open, occupancy and serviced to 0, set waiter to idle and locaton to table 1
	majorsbar.open = 1;
	majorsbar.wtr.status = 'I';
	majorsbar.wtr.table = 1;
	majorsbar.occupancy = 0;
	majorsbar.serviced = 0;
}

// add group function
static int add_group(int p, int t) {
	// create new group pointer and allocate enough memory within kernel
	group *g; 
	g = kmalloc(sizeof(group), (__GFP_RECLAIM | __GFP_IO | __GFP_FS));
	
	// create random group id
	int id = get_random_int();
	char type;
	
	// convert int type from syscall reference to char
	switch(t) {
		case 0:
			type = 'F';
			break;
		case 1:
			type = 'O';
			break;
		case 2:
			type = 'J';
			break;
		case 3:
			type = 'S';
			break;
		case 4:
			type = 'P';
			break;
		default:
			type = 'X';
	}
	
	// save data to group
	g->type = type;
	g->group_id = id;
	g->amt = p;
	
	// add group to list
	mutex_lock(&lock);
	list_add_tail(&g->list, &majorsbar.queue);
	mutex_unlock(&lock);
	
	// update majorsbar stats, guests waiting and groups waiting should increase accordingly
	majorsbar.waiting = majorsbar.waiting + p;
	majorsbar.waiting_groups = majorsbar.waiting_groups + 1;
	
	return 0;
}

/*
* proc functions
*/

// print groups function -> debug function not currently used
static int proc_groups(group *g) 
{

	//char* buf;
	char buf1[3];
	char buf2[12];

	
	int i;
	strcpy(buf1, "");
	strcpy(buf2, "");
	str = (char*) kmalloc(sizeof(char) * ((g->amt * 2) + 30) , GFP_KERNEL);
	str[0] = '\0';
	
	//buf = (char*) kmalloc(sizeof(char) * 3, GFP_KERNEL);

	for (i = 0; i < g->amt; i++)
	{
		sprintf(buf1, "%c ", g->type);
		strcat(str, buf1);
	}
	
	//buf = (char*) kmalloc(sizeof(char) * 12, GFP_KERNEL);
	// "(group id: " + nullterminator = 11 chars
	
	sprintf(buf2, "(group id: ");
	strcat(str, buf2);
	
	//buf = (char*) kmalloc(sizeof(char) * 3, GFP_KERNEL);
	// "x)" + nullterminator = 3 chars

	sprintf(buf1, "%u)", g->group_id);
	strcat(str, buf1);
	
	return 0;
	
}

// proc_statuses proc function -> stanley
static void proc_statuses(table t)
{
	//char strl[16];
	char buf[3];
	int i;
	strcpy(buf, "");
	str = (char*) kmalloc(sizeof(char) * 17, GFP_KERNEL);
	str[0] = '\0';
	
	for (i = 0; i < 8; i++)
	{
		sprintf(buf, "%c ", t.seats[i].status);
		strcat(str, buf);
	}
}

// bar_status function -> stanley
static void bar_status(void)
{
	char buf[5];
	int i;
	int j;
	strcpy(buf, "");

	//declare two parallel arrays	
	int counter[4] = { 0, 0, 0, 0};
	char statuses[4] ={ 'C', 'C', 'C', 'C'};
	
	str = (char*) kmalloc(sizeof(char) * 17, GFP_KERNEL); //max output of this can be
		     //3 f, 3 f, 3 f, 3 f, which is 19 characters including null terminator
	str[0] = '\0'; // this prevents random stuff from memory being cat into str
	
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 8; j++)
		{
			if (majorsbar.tables[i].seats[j].status != 'C' &&  majorsbar.tables[i].seats[j].status != 'D')
			{
				counter[i]++;
				statuses[i] = majorsbar.tables[i].seats[j].status;
			}
		}
	}	

	for (i = 3; i >= 0; i--)
	{
		if (statuses[i] != 'C' && counter[i] != 0)
		{
			sprintf(buf, "%d %c ", counter[i], statuses[i]);
			strcat(str, buf);	
		} 	
	}
}

/*
* waiter stuff
*/

// simulate waiter function -> thread dependent
static int simulate_waiter(void *pv) {
	int pos = 0; // initial position of the waiter
		
	while(!kthread_should_stop()) {
		if(to_do() == 0) { // if there is nothing to do in the bar
			majorsbar.wtr.status = 'I'; // set to idle
		} else { // there is something to do within the bar
			if(is_occupied(pos) == 0) { // table is not occupied
				if(is_clean(pos) == 1) { // table it not occupied and clean
					if(majorsbar.waiting != 0) { // there are people waiting
						majorsbar.wtr.status = 'L'; // waiter will begin loading
						msleep(1000);
						load_table(pos);
					}
				} else { // table is not occupied and not clean
					majorsbar.wtr.status = 'C'; // waiter will begin cleaning
					msleep(10000);
					clean_table(pos);
				}
			} else { // table is occupied
				shrink_table(pos); // attempt to remove customers if possible
			}
			
			pos++; // move to next table
			if(pos == 4) // reset postion counter to simulate circular motion
				pos = 0;
			
			majorsbar.wtr.status = 'M'; // waiter is moving
			msleep(2000);
			majorsbar.wtr.table = pos + 1;
			
		}
	}
	return 0;
}

// load table function
static int load_table(int pos) {
	// get first group in the queue
	group *g;
	g = list_first_entry(&majorsbar.queue, group, list);
	
	printk(KERN_ALERT "Waiter sat group of %d of type %c", g->amt, g->type);
	
	// store relevant guest information pertaining to the group being loaded
	int guest_quantity = g->amt;
	int guest_id = g->group_id;
	char guest_type = g->type;
	struct timespec64 current_time;
	ktime_get_real_ts64(&current_time);
	long guest_time = current_time.tv_sec - initial_time.tv_sec;
	int i;
	
	// update table with new seat information
	for(i = 0; i < guest_quantity; i++) {
		majorsbar.tables[pos].seats[i].status = guest_type;
		majorsbar.tables[pos].seats[i].g.type = guest_type;
		majorsbar.tables[pos].seats[i].g.group_id = guest_id;
		majorsbar.tables[pos].seats[i].g.time = guest_time;
	}
	
	// remove group from list
	mutex_lock(&lock);
	list_del(&g->list);
	mutex_unlock(&lock);
	
	// update bar stats
	majorsbar.waiting = majorsbar.waiting - guest_quantity;
	majorsbar.occupancy = majorsbar.occupancy + guest_quantity;
	majorsbar.waiting_groups = majorsbar.waiting_groups - 1;
	
	
	return 0;	
}

// clean table function
static int clean_table(int pos) {
	// loop through all the seats at table #pos and set all seats status to clean
	int i;
	for(i = 0; i < 8; i++) {
		majorsbar.tables[pos].seats[i].status = 'C';
	}
	
	return 0;
}

// shrink table function
static int shrink_table(int pos) {
	// get current time
	struct timespec64 current_time;
	ktime_get_real_ts64(&current_time);
	long elapsed = current_time.tv_sec - initial_time.tv_sec;
	int i;	
	
	// check through all the seats to remove a guest
	for(i = 0; i < 8; i++) {
		if(majorsbar.tables[pos].seats[i].status != 'C' && majorsbar.tables[pos].seats[i].status != 'D') {
			// if able to remove a guest set the waiter to load and update the seat status to dirty
			if(remove_guest(majorsbar.tables[pos].seats[i].status, majorsbar.tables[pos].seats[i].g.time, elapsed) == 1) {
				majorsbar.wtr.status = 'L';
				msleep(1000);
				majorsbar.tables[pos].seats[i].status = 'D';
				majorsbar.serviced = majorsbar.serviced + 1;
				majorsbar.occupancy = majorsbar.occupancy - 1;
			}
		}
	}
	return 0;	
}

//remove guest function
static int remove_guest(char guest_type, long here, long now) {
	// compares the difference between when a guest arrived and the current time the waiter is checking
	int r = 0;
	
	switch(guest_type) {
		case 'F':
		{
			if((now - here) >= 5) // if the freshman has been here for at least 5 seconds can remove
				r = 1;
			break;
		}
		case 'O':
		{
			if((now - here) >= 10) // sophomore -> 10 seconds
				r = 1;
			break;
		}
		case 'J':
		{
			if((now - here) >= 15) // junior -> 15 seconds
				r = 1;
			break;
		}
		case 'S':
		{
			if((now - here) >= 20) // senior -> 20 seconds
				r = 1;
			break;
		}
		case 'P':
		{
			if((now - here) >= 25) // professor/grad -> 25 seconds
				r = 1;
			break;
		}
		default:
			r = 0; // otherwise just ignore
	}
	
	return r;
}

// to do function
static int to_do(void) {
	// check if there are guests waiting
	if(majorsbar.waiting != 0)
		return 1;

	// check if there are seats to clean
	int i;
	int j;	
	for(i = 0; i < 4; i++)
		for(j = 0; j < 8; j++)
			if(majorsbar.tables[i].seats[j].status != 'C')
				return 1;
	return 0;
	
}

// is occupied function
static int is_occupied(int pos) {
	// check if any of the seats are occupied at table # pos
	int i;
	for(i = 0; i < 8; i++) {
		if(majorsbar.tables[pos].seats[i].status != 'C' && majorsbar.tables[pos].seats[i].status != 'D')
			return 1;
	}
	return 0;
}

// is clean function
static int is_clean(int pos) {
	// checks if table is completely clean
	int i;
	for(i = 0; i < 8; i++) {
		if(majorsbar.tables[pos].seats[i].status != 'C')
			return 0;
	}
	
	return 1;
}

/*
* module stuff
*/

static ssize_t majorsbar_read(struct file* file, char* ubuf, size_t count, loff_t *ppos)
{
	char buf[1024];
	int len = 0;

	if (read)
	{
		read = false;
		return 0;
	}

	struct timespec64 current_time;
	ktime_get_real_ts64(&current_time);

	long elapsedTimesec = current_time.tv_sec - initial_time.tv_sec;	
	
	len += snprintf(buf + len, sizeof(buf) - len, "Waiter state: %c\n", majorsbar.wtr.status);
	len += snprintf(buf + len, sizeof(buf) - len, "Current table: %d\n", majorsbar.wtr.table);
	len += snprintf(buf + len, sizeof(buf) - len, "Elapsed time: %ld seconds\n", elapsedTimesec);
	len += snprintf(buf + len, sizeof(buf) - len, "Current occupancy: %d\n", majorsbar.occupancy);
	len += snprintf(buf + len, sizeof(buf) - len, "Bar status: ");
	bar_status();
	len += snprintf(buf + len, sizeof(buf) - len, "%s\n", str);
	// function that displays that current groups at the table.
	// starts from tables 4 and then down.
	// eg 3 F, 6 S, 3 P	

	len += snprintf(buf + len, sizeof(buf) - len, "Number of customers waiting: %d\n", majorsbar.waiting);
	len += snprintf(buf + len, sizeof(buf) - len, "Number of groups waiting: %d\n", majorsbar.waiting_groups);
	len += snprintf(buf + len, sizeof(buf) - len, "Contents of queue:\n");
	
	
	int i = 0;
	group *g;
	struct list_head *temp;
	list_for_each(temp, &majorsbar.queue)
	{
		g = list_entry(temp, group, list);
		proc_groups(g);	
		len += snprintf(buf + len, sizeof(buf) - len, "%s\n", str);
		i++;
	}
	
		
	// S S S S S (group id: 89456)
	// P P P P P (group id: 90323)

	// table 4
	len += snprintf(buf + len, sizeof(buf) - len, "[");
	if (majorsbar.wtr.table == 4) 
	{
		len += snprintf(buf + len, sizeof(buf) - len, "*");
		len += snprintf(buf + len, sizeof(buf) - len, "]");
	}
	else
	len += snprintf(buf + len, sizeof(buf) - len, " ]");
	proc_statuses(majorsbar.tables[3]);
	len += snprintf(buf + len, sizeof(buf) - len, " Table 4: %s\n", str);
	
	// function that determines the array of occupied seat characters

	// table 3
	len += snprintf(buf + len, sizeof(buf) - len, "[");
        if (majorsbar.wtr.table == 3) 
        {
                len += snprintf(buf + len, sizeof(buf) - len, "*");
                len += snprintf(buf + len, sizeof(buf) - len, "]");
        }
        else
        len += snprintf(buf + len, sizeof(buf) - len, " ]");
	proc_statuses(majorsbar.tables[2]);
        len += snprintf(buf + len, sizeof(buf) - len, " Table 3: %s\n", str);


	// table 2	
	len += snprintf(buf + len, sizeof(buf) - len, "[");
        if (majorsbar.wtr.table == 2)
        {
                len += snprintf(buf + len, sizeof(buf) - len, "*");
                len += snprintf(buf + len, sizeof(buf) - len, "]");
        }
        else
        len += snprintf(buf + len, sizeof(buf) - len, " ]");
	proc_statuses(majorsbar.tables[1]);
        len += snprintf(buf + len, sizeof(buf) - len, " Table 2: %s\n", str);

	
	// table 1
	len += snprintf(buf + len, sizeof(buf) - len, "[");
        if (majorsbar.wtr.table == 1)
        {
                len += snprintf(buf + len, sizeof(buf) - len, "*");
                len += snprintf(buf + len, sizeof(buf) - len, "]");
        }
        else
        len += snprintf(buf + len, sizeof(buf) - len, " ]");
	proc_statuses(majorsbar.tables[0]);
        len += snprintf(buf + len, sizeof(buf) - len, " Table 1: %s\n", str);

	if (copy_to_user(ubuf, buf, len))
    	{
		return -EFAULT;
    	}

	read = true;
	*ppos = len;
    return len;
}

static struct proc_ops procfile_fops =
{
	.proc_read = majorsbar_read,
}; 

static int init_mod(void) {
	STUB_initialize_bar = open_bar;
	STUB_customer_arrival = arrival;
	STUB_close_bar = shutdown_bar;
	INIT_LIST_HEAD(&majorsbar.queue);

	proc_entry = proc_create("majorsbar", 0666, NULL, &procfile_fops);
	
	thread = kthread_create(simulate_waiter, NULL, "[waiter]");
	
	return 0;
}

static void exit_mod(void) {
	STUB_initialize_bar = NULL;
	STUB_customer_arrival = NULL;
	STUB_close_bar = NULL;
	
	kthread_stop(thread);
	
	return;
}

module_init(init_mod);
module_exit(exit_mod);
