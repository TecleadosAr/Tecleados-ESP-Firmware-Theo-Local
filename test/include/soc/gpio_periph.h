/* SOC shim — stub for host testing */
#pragma once

#define GPIO_IS_VALID_GPIO(gpio) ((gpio) >= 0 && (gpio) <= 48)
#define GPIO_IS_VALID_OUTPUT_GPIO(gpio) ((gpio) >= 0 && (gpio) <= 48)
