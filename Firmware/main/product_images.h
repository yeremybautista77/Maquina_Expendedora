/**
 * @file product_images.h
 * @brief Imagenes de los 6 productos embebidas en flash.
 *
 * ARCHIVO GENERADO: no editar a mano. Ejecuta
 * herramientas/generar_imagenes.ps1 para regenerarlo.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Cantidad de imagenes generadas (una por canal). */
#define PRODUCT_IMAGE_COUNT     6

/** Lado en pixeles de cada imagen generada. */
#define PRODUCT_IMAGE_SIZE      52

/**
 * @brief Devuelve la imagen del producto indicado.
 *
 * @param index Canal 0..PRODUCT_IMAGE_COUNT-1.
 * @return Descriptor LVGL en flash, o NULL si el indice no es valido.
 */
const lv_img_dsc_t *product_images_get(uint8_t index);

#ifdef __cplusplus
}
#endif
