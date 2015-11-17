/* vim: set nolist ts=8 sw=8 : */

#include <asm/io.h> /* readl, writel */
#include <asm/uaccess.h>
#include <linux/file.h>
#include <linux/fs.h> /* file_operations */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <linux/miscdevice.h> /* miscdevice */
#include <linux/module.h>
#include <linux/of.h> /*of_property_read_u32 */
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/serial_reg.h> /* serial register macros */
#include <linux/wait.h>

#define RW_BUF_SIZE 64
#define SERIAL_RESET_COUNTER 0
#define SERIAL_GET_COUNTER 1

/* table of compatible devices */
static const struct of_device_id fes_of_ids[] = {
	{ .compatible = "free-electrons,serial" },
	{}
};

/* private device structure */
struct feserial_dev {
	struct platform_device *pdev;
	struct miscdevice miscdev;
	void __iomem *regs;
	unsigned long xmit_cnt;
	int irq;
	char cbuf[RW_BUF_SIZE];
	int cr, cw;
	wait_queue_head_t serial_wait;
};

static unsigned int reg_read(struct feserial_dev *, int);
static void reg_write(struct feserial_dev *, int, int);

static void uart_char_tx(struct feserial_dev *fesdev, char c)
{
	while((reg_read(fesdev, UART_LSR) & UART_LSR_THRE) == 0x00) {
		cpu_relax();
	}

	reg_write(fesdev, c, UART_TX);
	fesdev->xmit_cnt++;
}

static void uart_write(struct feserial_dev *dev, char * ubuf, size_t size)
{
	int i;
	for (i = 0; i < size; i++) {
		uart_char_tx(dev, ubuf[i]);
		if (ubuf[i] == '\n')
			uart_char_tx(dev, '\r');
	}
}

/* file operations */
static ssize_t feserial_read(struct file *f, char __user *ubuf, size_t size,
	loff_t *off)
{
	struct feserial_dev *fdev;
	char c;
	char r = '\r';

	fdev = container_of(f->private_data, struct feserial_dev, miscdev);
	wait_event_interruptible(fdev->serial_wait,
		fdev->cr != fdev->cw);

	if (fdev->cr != fdev->cw) {
		c = fdev->cbuf[fdev->cr++];
		//pr_info("reading %c\n", c);
		if (copy_to_user(ubuf, &c, 1)) {
			pr_err("copy_to_user failed\n");
			return -EINVAL;
		}
		if (fdev->cr == RW_BUF_SIZE)
			fdev->cr = 0;
	}
	return 1; // read 1 byte at a time
}

/* file is in userspace ! */
static ssize_t feserial_write(struct file *f, const char __user *ubuf,
	size_t size, loff_t *off)
{
	char kbuf[RW_BUF_SIZE];
	unsigned int chunk = RW_BUF_SIZE;

	size_t left_to_copy = size;
	ssize_t copied = 0;

	struct feserial_dev *dev;

	/* get the feserial_dev structure from file */
	if (f->private_data == NULL)
	{
		pr_err("private_data is empty!\n");
		return -1;
	}
	dev = container_of(f->private_data, struct feserial_dev, miscdev);
	if (dev == NULL)
	{
		pr_err("could not retrieve miscdev structure\n");
	}

	while (left_to_copy > 0) {
		if (size < RW_BUF_SIZE)
			chunk = (unsigned int) size;

		if (copy_from_user(kbuf, ubuf, chunk)) {
			pr_err("copy_from_user failed\n");
			return -1;
		}

		left_to_copy -= chunk;
		uart_write(dev, kbuf, chunk);
		copied += chunk;
	}

	return copied;
}

static long feserial_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct feserial_dev *dev;
	void __user *argp = (void __user *) arg;

	dev = container_of(f->private_data, struct feserial_dev, miscdev);

	switch(cmd) {
	case SERIAL_RESET_COUNTER:
		dev->xmit_cnt = 0;
		break;
	case SERIAL_GET_COUNTER:
		if (copy_to_user(argp, &dev->xmit_cnt, sizeof(dev->xmit_cnt)))
			return -EFAULT;
		break;
	default:
		return -ENOTTY;
	}
	return 0;
}

/*
 * need a open operation to be defined in order to access data field
 * file->private_data
 */
static int feserial_open(struct inode *i, struct file *f)
{
	return 0;
}

static const struct file_operations fes_fops = {
	.read = feserial_read,
	.write = feserial_write,
	.unlocked_ioctl = feserial_ioctl,
	.open = feserial_open,
};
/* end of file operations */

static irqreturn_t feserial_handler(int irq, void *dev)
{
	struct feserial_dev *fdev;

	fdev = (struct feserial_dev*) dev;
	fdev->cbuf[fdev->cw] = (char) reg_read(fdev, UART_RX);

	fdev->cw++;
	if (fdev->cw == RW_BUF_SIZE)
		fdev->cw = 0;

	wake_up(&fdev->serial_wait);
	return IRQ_HANDLED;
}

static unsigned int reg_read(struct feserial_dev *fesdev, int offset)
{
	return readl(fesdev->regs + 4*offset);
}

static void reg_write(struct feserial_dev *fesdev, int val, int offset)
{
	writel(val, fesdev->regs + 4*offset);
}

static int feserial_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct feserial_dev *dev;
	unsigned int baud_divisor, uartclk;
	int retval;

	pr_info("Called feserial_probe (v%s)\n", utsname()->release);

	/* enable device-level Power Management */
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/*
	 * see include/linux/ioport.h,
	 * resource types include IO ports, memory, register offsets IRQs
	 * ,DMAs, Buses , etc...
	*/
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		pr_err("Unable to obtain platform memory resource\n");
		return -1;
	}

	/*
	 * memory allocated with devm_* is associated with the device. When
	 * the device is detached/removed, the resource is freed. Last
	 * parameter is Get Free Page (GFP) Flags structure
	*/
	dev = devm_kzalloc(&pdev->dev, sizeof(struct feserial_dev), GFP_KERNEL);
	if (!dev) {
		pr_err("Unable to obtain kernel memory\n");
		return -ENOMEM;
	}
	dev->pdev = pdev;
	dev->xmit_cnt = 0;

	/* register receive interrupt handler */
	dev->irq = platform_get_irq(pdev, 0);
	retval = devm_request_irq(&pdev->dev, dev->irq, feserial_handler, 0,
		pdev->name, dev);

	/* recieve buffer initialization */
	dev->cr = 0;
	dev->cw = 0;

	/* initialize wait queue! */
	init_waitqueue_head(&dev->serial_wait);

	/*
	 * map physical memory associated with the resource to kernel
	 * virtual address space
	*/
	dev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (!dev->regs) {
		dev_err(&pdev->dev, "Cannot remap registers\n");
		return -ENOMEM;
	}

	/* misc device registration */
	dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	dev->miscdev.name = kasprintf(GFP_KERNEL, "feserial-%x",
		res->start);
	dev->miscdev.fops = &fes_fops;
	platform_set_drvdata(pdev, dev);

	dev = (struct feserial_dev *) platform_get_drvdata(pdev);

	/* Baud-rate configuration */
	of_property_read_u32(pdev->dev.of_node, "clock-frequency", &uartclk);
	baud_divisor = uartclk / 16 / 115200;
	reg_write(dev, 0x07, UART_OMAP_MDR1);
	reg_write(dev, 0x00, UART_LCR);
	reg_write(dev, UART_LCR_DLAB, UART_LCR);
	reg_write(dev, baud_divisor & 0xff, UART_DLL);
	reg_write(dev, (baud_divisor >> 8) & 0xff, UART_DLM);
	reg_write(dev, UART_LCR_WLEN8, UART_LCR);
	reg_write(dev, UART_IER_RDI, UART_IER);

	/* trigger a soft reset */
	reg_write(dev, UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT, UART_FCR);
	reg_write(dev, 0x00, UART_OMAP_MDR1);

	retval = misc_register(&dev->miscdev);
	if (retval != 0) {
		pr_err("Failed to register misc console\n");
		return -ENODEV;
	}

	return 0;
}

static int feserial_remove(struct platform_device *pdev)
{
	struct feserial_dev *dev;
	pr_info("Called feserial_remove\n");
	pm_runtime_disable(&pdev->dev);
	dev = platform_get_drvdata(pdev);
	if (dev == NULL) {
		pr_err("Platform feserial_dev data is empty!\n");
		return -ENODEV;
	}
	misc_deregister(&dev->miscdev);
	kfree(dev->miscdev.name);
	return 0;
}

static struct platform_driver feserial_driver = {
	.driver = {
		.name = "feserial",
		.owner = THIS_MODULE,
		.of_match_table = fes_of_ids,
	},
	.probe = feserial_probe,
	.remove = feserial_remove,
};

module_platform_driver(feserial_driver);
MODULE_LICENSE("GPL");
