// SPDX-License-Identifier: GPL-2.0
/*
 *  cpuidle-test - Test driver for cpuidle.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/cpuidle.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/sched/idle.h>
#include <linux/sched/clock.h>
#include <linux/sched/idle.h>

#define CPUIDLE_STATE_MAX	10
#define MAX_PARAM_LENGTH	100

static unsigned int nr_states = 4;
static unsigned int sim_type = 1;
static char name[MAX_PARAM_LENGTH];
static char latency_us[MAX_PARAM_LENGTH];
static char residency_us[MAX_PARAM_LENGTH];


module_param(nr_states, uint, 0644);
module_param(sim_type, uint, 0644);
module_param_string(name, name, MAX_PARAM_LENGTH, 0644);
module_param_string(latency_us, latency_us, MAX_PARAM_LENGTH, 0644);
module_param_string(residency_us, residency_us, MAX_PARAM_LENGTH, 0644);

static struct cpuidle_driver test_cpuidle_driver = {
	.name		= "test_cpuidle",
	.owner		= THIS_MODULE,
};

static struct cpuidle_state *cpuidle_state_table __read_mostly;

static struct cpuidle_device __percpu *test_cpuidle_devices;
static enum cpuhp_state test_hp_idlestate;


static int __cpuidle idle_loop(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	u64 time_start;

	local_irq_enable();

	time_start = local_clock();
	/*
	 * Simulating entry latency into idle state.
	 */
	while (local_clock() - time_start < drv->states[index].exit_latency) {
	}

	if (!current_set_polling_and_test()) {
		while (!need_resched())
			cpu_relax();
	}

	time_start = local_clock();
	/*
	 * Simulating exit latency from idle state.
	 */
	while (local_clock() - time_start < drv->states[index].exit_latency) {
	}

	current_clr_polling();

	return index;
}

	/*
	 * Defining user specified custome set of idle states.
	 */
static struct cpuidle_state cpuidle_states[CPUIDLE_STATE_MAX] = {
	{	.name = "snooze",
		.exit_latency = 0,
		.target_residency = 0,
		.enter = idle_loop },
};

static struct cpuidle_state cpuidle_states_ppc[] = {
	{	.name = "snooze",
		.exit_latency = 0,
		.target_residency = 0,
		.enter = idle_loop },
	{
		.name = "stop0",
		.exit_latency = 2,
		.target_residency = 20,
		.enter = idle_loop },
	{
		.name = "stop1",
		.exit_latency = 5,
		.target_residency = 50,
		.enter = idle_loop },
	{
		.name = "stop2",
		.exit_latency = 10,
		.target_residency = 100,
		.enter = idle_loop },
};

static struct cpuidle_state cpuidle_states_intel[] = {
	{	.name = "poll",
		.exit_latency = 0,
		.target_residency = 0,
		.enter = idle_loop },
	{
		.name = "c1",
		.exit_latency = 2,
		.target_residency = 2,
		.enter = idle_loop },
	{
		.name = "c1e",
		.exit_latency = 10,
		.target_residency = 20,
		.enter = idle_loop },
	{
		.name = "c3",
		.exit_latency = 80,
		.target_residency = 211,
		.enter = idle_loop },
};

static int cpuidle_cpu_online(unsigned int cpu)
{
	struct cpuidle_device *dev;

	dev = per_cpu_ptr(test_cpuidle_devices, cpu);
	if (!dev->registered) {
		dev->cpu = cpu;
		if (cpuidle_register_device(dev)) {
			pr_notice("cpuidle_register_device %d failed!\n", cpu);
			return -EIO;
		}
	}

	return 0;
}

static int cpuidle_cpu_dead(unsigned int cpu)
{
	struct cpuidle_device *dev;

	dev = per_cpu_ptr(test_cpuidle_devices, cpu);
	if (dev->registered)
		cpuidle_unregister_device(dev);

	return 0;
}

static int cpuidle_driver_init(void)
{
	int idle_state;
	struct cpuidle_driver *drv = &test_cpuidle_driver;

	drv->state_count = 0;

	for (idle_state = 0; idle_state < nr_states; ++idle_state) {
		/* Is the state not enabled? */
		if (cpuidle_state_table[idle_state].enter == NULL)
			continue;

		drv->states[drv->state_count] =	/* structure copy */
			cpuidle_state_table[idle_state];

		drv->state_count += 1;
	}

	return 0;
}

static int add_cpuidle_states(void)
{
	/* Parse the module param and initialize the idle states here
	 * in cpuidle_state_table.
	 */
	char *this_param;
	char *input_name = name;
	char *input_res = residency_us;
	char *input_lat = latency_us;
	int index = 1;
	long temp;
	int rc;

	switch (sim_type) {
	case 1:
		cpuidle_state_table = cpuidle_states_ppc;
		return 0;
	case 2:
		cpuidle_state_table = cpuidle_states_intel;
		return 0;
	case 3:
		break;
	default:
		pr_warn("Sim value out of bound\n");
		break;
	}

	if (strnlen(input_name, MAX_PARAM_LENGTH)) {
		while ((this_param = strsep(&input_name, ",")) && index <= nr_states) {
			strcpy(cpuidle_states[index].name, this_param);
			cpuidle_states[index].enter = idle_loop;
			index++;
		}
	}

	if (strnlen(input_res, MAX_PARAM_LENGTH)) {
		index = 1;
		while ((this_param = strsep(&input_res, ",")) && index <= nr_states) {
			rc = kstrtol(this_param, 10, &temp);
			cpuidle_states[index].target_residency = temp;
			index++;
		}
	}

	if (strnlen(input_lat, MAX_PARAM_LENGTH)) {
		index = 1;
		while ((this_param = strsep(&input_lat, ",")) && index <= nr_states) {
			rc = kstrtol(this_param, 10, &temp);
			cpuidle_states[index].exit_latency = temp;
			index++;
		}
	}

	cpuidle_state_table = cpuidle_states;
	return nr_states;
}

static void test_cpuidle_uninit(void)
{
	if (test_hp_idlestate)
		cpuhp_remove_state(test_hp_idlestate);
	cpuidle_unregister_driver(&test_cpuidle_driver);

	free_percpu(test_cpuidle_devices);
	test_cpuidle_devices = NULL;
}

static int __init test_cpuidle_init(void)
{
	int retval;

	add_cpuidle_states();
	cpuidle_driver_init();
	retval = cpuidle_register(&test_cpuidle_driver, NULL);
	if (retval) {
		printk(KERN_DEBUG "Registration of test driver failed.\n");
		return retval;
	}

	test_cpuidle_devices = alloc_percpu(struct cpuidle_device);
	if (test_cpuidle_devices == NULL) {
		cpuidle_unregister_driver(&test_cpuidle_driver);
		return -ENOMEM;
	}

	retval = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					   "cpuidle/powernv:online",
					   cpuidle_cpu_online,
					   cpuidle_cpu_dead);

	if (retval < 0) {
		test_cpuidle_uninit();
	} else {
		test_hp_idlestate = retval;
		retval = 0;
	}

	return retval;
}

static void __exit test_cpuidle_exit(void)
{
	test_cpuidle_uninit();
}

module_init(test_cpuidle_init);
module_exit(test_cpuidle_exit);
MODULE_DESCRIPTION("Test Cpuidle Driver");
MODULE_AUTHOR("Abhishek Goel");
MODULE_LICENSE("GPL");

