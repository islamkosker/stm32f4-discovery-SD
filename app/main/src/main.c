/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "main.h"

int main(void)
{
    int ret = init_adc();
    printk("err: %d", ret);
}