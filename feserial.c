/* vim: set nolist ts=8 sw=8 : */

#include <asm/io.h> /* readl, writel */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h> /*of_property_read_u32 */
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/serial_reg.h> /* serial register macros */

/* table of compatible devices */
static const struct of_device_id fes_of_ids[] = {
	{ .compatible = "free-electrons,serial" },
	{}
};

/* private device structure */
struct feserial_dev {
	void __iomem *regs;
};

static unsigned int reg_read(struct feserial_dev *fesdev, int offset)
{
	return readl(fesdev->regs + 4*offset);
}

static void reg_write(struct feserial_dev *fesdev, int val, int offset)
{
	writel(val, fesdev->regs + 4*offset);
}

static void test_write(struct feserial_dev *fesdev, char c)
{
	while((reg_read(fesdev, UART_LSR) & UART_LSR_THRE) == 0x00) {
		cpu_relax();
	}
	reg_write(fesdev, c, UART_TX);
}

static int feserial_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct feserial_dev *dev;
	unsigned int baud_divisor, uartclk;

	pr_info("Called feserial_probe\n");

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
	pr_info("device is located at physical address 0x%x\n", res->start);

	/*
	 * memory allocated with devm_* is associated with the device. When
	 * the device is detached/removed, the resource is freed. Last
	 * parameter is Get Free Page (GFP) Flags structure
	*/
	dev = devm_kzalloc(&pdev->dev, sizeof(struct feserial_dev), GFP_KERNEL);
	if (!dev)
		pr_err("Unable to obtain kernel memory\n");

	/*
	 * map physical memory associated with the resource to kernel
	 * virtual address space
	*/
	dev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (!dev->regs) {
		dev_err(&pdev->dev, "Cannot remap registers\n");
		return -ENOMEM;
	}

	/* enable device-level Power Management */
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/* Baud-rate configuration */
	of_property_read_u32(pdev->dev.of_node, "clock-frequency", &uartclk);
	baud_divisor = uartclk / 16 / 115200;
	reg_write(dev, 0x07, UART_OMAP_MDR1);
	reg_write(dev, 0x00, UART_LCR);
	reg_write(dev, UART_LCR_DLAB, UART_LCR);
	reg_write(dev, baud_divisor & 0xff, UART_DLL);
	reg_write(dev, (baud_divisor >> 8) & 0xff, UART_DLM);
	reg_write(dev, UART_LCR_WLEN8, UART_LCR);

	/* trigger a soft reset */
	reg_write(dev, UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT, UART_FCR);
	reg_write(dev, 0x00, UART_OMAP_MDR1);

	/* test write */
	test_write(dev, 's');
	test_write(dev, 't');
	test_write(dev, 'e');
	test_write(dev, 'p');
	test_write(dev, 'h');
	test_write(dev, 'e');
	test_write(dev, 'n');
	test_write(dev, '\n');
	return 0;
}

static int feserial_remove(struct platform_device *pdev)
{
	pr_info("Called feserial_remove\n");
	pm_runtime_disable(&pdev->dev);
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
