/**
 * @file stm32_gpio_cube.c
 * @brief
 * @author Denis Deryugin <deryugin.denis@gmail.com>
 * @version 0.1
 * @date 2016-02-12
 */

#include <assert.h>
#include <string.h>

#include <embox/unit.h>
#include <util/bit.h>
#include <util/array.h>

#include <kernel/irq.h>

#include <drivers/gpio/stm32.h>
#include <drivers/gpio/gpio_driver.h>

EMBOX_UNIT_INIT(stm32_gpio_init);

#define STM32_GPIO_CHIP_ID OPTION_GET(NUMBER,gpio_chip_id)

#define EXTI0_IRQ          OPTION_GET(NUMBER, exti0_irq)
static_assert(EXTI0_IRQ == EXTI0_IRQn, "");

#define EXTI1_IRQ          OPTION_GET(NUMBER, exti1_irq)
static_assert(EXTI1_IRQ == EXTI1_IRQn, "");

#define EXTI2_IRQ          OPTION_GET(NUMBER, exti2_irq)
#if (EXTI2_IRQ != 0)
#if defined(STM32F303xC)
static_assert(EXTI2_IRQ == EXTI2_TSC_IRQn, "");
#else
static_assert(EXTI2_IRQ == EXTI2_IRQn, "");
#endif
#endif /*(EXTI2_IRQ != 0)*/

#define EXTI3_IRQ          OPTION_GET(NUMBER, exti3_irq)
static_assert(EXTI3_IRQ == EXTI3_IRQn, "");

#define EXTI4_IRQ          OPTION_GET(NUMBER, exti4_irq)
static_assert(EXTI4_IRQ == EXTI4_IRQn, "");

#define EXTI9_5_IRQ        OPTION_GET(NUMBER, exti9_5_irq)
static_assert(EXTI9_5_IRQ == EXTI9_5_IRQn, "");

#define EXTI15_10_IRQ     OPTION_GET(NUMBER, exti15_10_irq)
static_assert(EXTI15_10_IRQ == EXTI15_10_IRQn, "");

static const unsigned char exti_irqs[] = {
	EXTI0_IRQ, EXTI1_IRQ, EXTI2_IRQ, EXTI3_IRQ, EXTI4_IRQ,
	EXTI9_5_IRQ, EXTI15_10_IRQ,
};

static struct gpio_chip stm32_gpio_chip;

static int stm32_gpio_setup_mode(unsigned char port, gpio_mask_t pins,
		int mode) {
	GPIO_InitTypeDef GPIO_InitStruct;

	assert(port < STM32_GPIO_PORTS_COUNT);

	stm32_gpio_clk_enable(stm32_gpio_ports[port]);

	memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));
	GPIO_InitStruct.Pin = pins;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	GPIO_InitStruct.Pull = GPIO_NOPULL;

	if (mode & GPIO_MODE_OUT) {
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;

		if (mode & GPIO_MODE_OUT_OPEN_DRAIN) {
			GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		}
		if (mode & GPIO_MODE_OUT_PUSH_PULL) {
			GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		}
	} else if (mode & GPIO_MODE_IN) {
		GPIO_InitStruct.Mode = GPIO_MODE_INPUT;

		if (mode & GPIO_MODE_IN_PULL_UP) {
			GPIO_InitStruct.Pull = GPIO_PULLUP;
		}
		if (mode & GPIO_MODE_IN_PULL_DOWN) {
			GPIO_InitStruct.Pull = GPIO_PULLDOWN;
		}
	} else if (mode & GPIO_MODE_OUT_ALTERNATE) {
#ifndef STM32F1_CUBE_GPIO /* There is no Alternate field in GPIO_InitTypeDef */
		GPIO_InitStruct.Alternate = GPIO_GET_ALTERNATE(mode);
#endif
		if (mode & GPIO_MODE_OUT_OPEN_DRAIN) {
			GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
		}
		if (mode & GPIO_MODE_OUT_PUSH_PULL) {
			GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		}

		if (mode & GPIO_MODE_IN_PULL_UP) {
			GPIO_InitStruct.Pull = GPIO_PULLUP;
		}
		if (mode & GPIO_MODE_IN_PULL_DOWN) {
			GPIO_InitStruct.Pull = GPIO_PULLDOWN;
		}
	} else if (mode & GPIO_MODE_INT_MODE_RISING_FALLING) {
		if ((mode & GPIO_MODE_INT_MODE_RISING_FALLING)
				== GPIO_MODE_INT_MODE_RISING_FALLING) {
			GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
		} else if (mode & GPIO_MODE_INT_MODE_RISING) {
			GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
		} else if (mode & GPIO_MODE_INT_MODE_FALLING) {
			GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
		}
	}

	HAL_GPIO_Init(stm32_gpio_ports[port], &GPIO_InitStruct);

	return 0;
}

static void stm32_gpio_set(unsigned char port, gpio_mask_t pins, char level) {
	assert(port < STM32_GPIO_PORTS_COUNT);
	HAL_GPIO_WritePin(stm32_gpio_ports[port], pins,
		level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static gpio_mask_t stm32_gpio_get(unsigned char port, gpio_mask_t pins) {
	gpio_mask_t res = 0;
	int bit;

	assert(port < STM32_GPIO_PORTS_COUNT);

	bit_foreach(bit, pins) {
		res |= HAL_GPIO_ReadPin(stm32_gpio_ports[port], 1 << bit) << bit;
	}

	return res;
}

irq_return_t stm32_gpio_irq_handler(unsigned int irq_nr, void *data) {
	int i;
	uint16_t pending;

	pending = __HAL_GPIO_EXTI_GET_FLAG(0xffff);

	__HAL_GPIO_EXTI_CLEAR_IT(pending);

	/* Notify all GPIO ports about interrupt */
	for (i = 0; i < STM32_GPIO_PORTS_COUNT; i++) {
		gpio_handle_irq(&stm32_gpio_chip, i, pending);
	}

	return IRQ_HANDLED;
}

static struct gpio_chip stm32_gpio_chip = {
	.setup_mode = stm32_gpio_setup_mode,
	.get = stm32_gpio_get,
	.set = stm32_gpio_set,
	.nports = STM32_GPIO_PORTS_COUNT
};

static int stm32_gpio_init(void) {
	int res, i;

	for (i = 0; i < ARRAY_SIZE(exti_irqs); i++) {
		if (0 == exti_irqs[i]) {
			continue;
		}
		res = irq_attach(exti_irqs[i], stm32_gpio_irq_handler, 0, NULL,
			"STM32 EXTI irq handler");
		if (res < 0) {
			return -1;
		}
	}

	return gpio_register_chip(&stm32_gpio_chip, STM32_GPIO_CHIP_ID);
}

STATIC_IRQ_ATTACH(EXTI0_IRQ, stm32_gpio_irq_handler, NULL);
STATIC_IRQ_ATTACH(EXTI1_IRQ, stm32_gpio_irq_handler, NULL);
#if (EXTI2_IRQ != 0)
STATIC_IRQ_ATTACH(EXTI2_IRQ, stm32_gpio_irq_handler, NULL);
#endif /* (EXTI2_IRQ != 0) */
STATIC_IRQ_ATTACH(EXTI3_IRQ, stm32_gpio_irq_handler, NULL);
STATIC_IRQ_ATTACH(EXTI4_IRQ, stm32_gpio_irq_handler, NULL);
STATIC_IRQ_ATTACH(EXTI9_5_IRQ, stm32_gpio_irq_handler, NULL);
STATIC_IRQ_ATTACH(EXTI15_10_IRQ, stm32_gpio_irq_handler, NULL);
