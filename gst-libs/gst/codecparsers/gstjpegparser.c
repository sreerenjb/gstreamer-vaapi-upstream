/*
 *  gstjpegparser.c - JPEG parser
 *
 *  Copyright (C) 2011-2012 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include <string.h>
#include <gst/base/gstbytereader.h>
#include "gstjpegparser.h"

#ifndef GST_DISABLE_GST_DEBUG

#define GST_CAT_DEFAULT ensure_debug_category()

static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("codecparsers_jpeg", 0,
        "GstJpegCodecParser");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}
#else

#define ensure_debug_category() /* NOOP */

#endif /* GST_DISABLE_GST_DEBUG */

#define DEBUG_PRINT_COMMENT 0

#define READ_UINT8(reader, val) G_STMT_START {                  \
    if (!gst_byte_reader_get_uint8 ((reader), &(val))) {        \
      GST_WARNING ("failed to read uint8");                     \
      goto failed;                                              \
    }                                                           \
  } G_STMT_END

#define READ_UINT16(reader, val) G_STMT_START {                 \
    if (!gst_byte_reader_get_uint16_be ((reader), &(val))) {    \
      GST_WARNING ("failed to read uint16");                    \
      goto failed;                                              \
    }                                                           \
  } G_STMT_END

#define READ_BYTES(reader, buf, length) G_STMT_START {          \
    const guint8 *vals;                                         \
    if (!gst_byte_reader_get_data (reader, length, &vals)) {    \
      GST_WARNING ("failed to read bytes, size:%d", length);    \
      goto failed;                                              \
    }                                                           \
    memcpy (buf, vals, length);                                 \
  } G_STMT_END

#define U_READ_UINT8(reader, val) G_STMT_START {                \
    (val) = gst_byte_reader_get_uint8_unchecked(reader);        \
  } G_STMT_END

#define U_READ_UINT16(reader, val) G_STMT_START {               \
    (val) = gst_byte_reader_get_uint16_be_unchecked(reader);    \
  } G_STMT_END


/* CCITT T.81, Annex K.1 Quantization tables for luminance and chrominance components */
/* only for 8-bit per sample image */
/* *INDENT-OFF* */
static const GstJpegQuantTables default_quant_tables_zigzag = {
  .quant_tables = {
    /* luma */
    {0, {0x10, 0x0b, 0x0c, 0x0e, 0x0c, 0x0a, 0x10, 0x0e,
         0x0d, 0x0e, 0x12, 0x11, 0x10, 0x13, 0x18, 0x27,
         0x1a, 0x18, 0x16, 0x16, 0x18, 0x31, 0x23, 0x25,
         0x1d, 0x28, 0x3a, 0x33, 0x3d, 0x3c, 0x39, 0x33,
         0x38, 0x37, 0x40, 0x48, 0x5c, 0x4e, 0x40, 0xa8,
         0x57, 0x45, 0x37, 0x38, 0x50, 0x6d, 0xb5, 0x57,
         0x5f, 0x62, 0x67, 0x68, 0x67, 0x3e, 0x4d, 0x71,
         0x79, 0x70, 0x64, 0x78, 0x5c, 0x65, 0x67, 0x63}, TRUE},
    /* chroma */
    {0, {0x11, 0x12, 0x12, 0x18, 0x15, 0x18, 0x2f, 0x1a,
         0x1a, 0x2f, 0x63, 0x42, 0x38, 0x42, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63}, TRUE},
    /* chroma */
    {0, {0x11, 0x12, 0x12, 0x18, 0x15, 0x18, 0x2f, 0x1a,
         0x1a, 0x2f, 0x63, 0x42, 0x38, 0x42, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
         0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63}, TRUE},
    {0,}
  }
};
/* *INDENT-ON* */

/* Table K.3: typical Huffman tables for 8-bit precision luminance and chrominance */
/* *INDENT-OFF* */
static const GstJpegHuffmanTables default_huf_tables = {
  .dc_tables = {
    /* DC luma */
    {{0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
     {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b},
     TRUE},
    /* DC chroma */
    {{0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00},
     {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b},
     TRUE},
    /* DC chroma */
    {{0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00},
     {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b},
     TRUE},
    {{0x0,},
     {0x0,}
    }
  },
  .ac_tables = {
    /* AC luma */
    {{0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
      0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d},
     {0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
      0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
      0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
      0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
      0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
      0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
      0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
      0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
      0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
      0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
      0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
      0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
      0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
      0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
      0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
      0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
      0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
      0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
      0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
      0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa},
     TRUE},
    /* AC chroma */
    {{0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
      0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77},
     {0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
      0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
      0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
      0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
      0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
      0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
      0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
      0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
      0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
      0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
      0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
      0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
      0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
      0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
      0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
      0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
      0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
      0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
      0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
      0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa},
     TRUE},
    /* AC chroma */
    {{0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
      0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77},
     {0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
      0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
      0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
      0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
      0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
      0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
      0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
      0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
      0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
      0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
      0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
      0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
      0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
      0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
      0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
      0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
      0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
      0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
      0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
      0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa},
     TRUE},
    {{0x00,},
     {0x00,}
    }
  }
};
/* *INDENT-ON* */

static inline gboolean
jpeg_parse_to_next_marker (GstByteReader * br, guint8 * marker)
{
  gint ofs;

  ofs = gst_jpeg_scan_for_marker_code (br->data, br->size, br->byte);
  if (ofs < 0)
    return FALSE;

  if (marker)
    *marker = br->data[ofs + 1];
  gst_byte_reader_skip (br, ofs - br->byte + 2);
  return TRUE;
}

gint
gst_jpeg_scan_for_marker_code (const guint8 * data, gsize size, guint offset)
{
  guint i;

  g_return_val_if_fail (data != NULL, -1);
  g_return_val_if_fail (size > offset, -1);

  for (i = offset; i < size - 1;) {
    if (data[i] != 0xff)
      i++;
    else {
      const guint8 v = data[i + 1];
      if (v >= 0xc0 && v <= 0xfe)
        return i;
      i += 2;
    }
  }
  return -1;
}

gboolean
gst_jpeg_parse_frame_hdr (GstJpegFrameHdr * frame_hdr,
    const guint8 * data, gsize size, guint offset)
{
  GstByteReader br;
  guint16 length;
  guint8 val;
  guint i;

  g_return_val_if_fail (frame_hdr != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size > offset, FALSE);

  size -= offset;
  gst_byte_reader_init (&br, &data[offset], size);
  g_return_val_if_fail (size >= 8, FALSE);

  U_READ_UINT16 (&br, length);  /* Lf */
  g_return_val_if_fail (size >= length, FALSE);

  U_READ_UINT8 (&br, frame_hdr->sample_precision);
  U_READ_UINT16 (&br, frame_hdr->height);
  U_READ_UINT16 (&br, frame_hdr->width);
  U_READ_UINT8 (&br, frame_hdr->num_components);
  g_return_val_if_fail (frame_hdr->num_components <=
      GST_JPEG_MAX_SCAN_COMPONENTS, FALSE);

  length -= 8;
  g_return_val_if_fail (length >= 3 * frame_hdr->num_components, FALSE);
  for (i = 0; i < frame_hdr->num_components; i++) {
    U_READ_UINT8 (&br, frame_hdr->components[i].identifier);
    U_READ_UINT8 (&br, val);
    frame_hdr->components[i].horizontal_factor = (val >> 4) & 0x0F;
    frame_hdr->components[i].vertical_factor = (val & 0x0F);
    U_READ_UINT8 (&br, frame_hdr->components[i].quant_table_selector);
    g_return_val_if_fail ((frame_hdr->components[i].horizontal_factor <= 4 &&
            frame_hdr->components[i].vertical_factor <= 4 &&
            frame_hdr->components[i].quant_table_selector < 4), FALSE);
    length -= 3;
  }

  g_assert (length == 0);
  return TRUE;
}

gboolean
gst_jpeg_parse_scan_hdr (GstJpegScanHdr * scan_hdr,
    const guint8 * data, gsize size, guint offset)
{
  GstByteReader br;
  guint16 length;
  guint8 val;
  guint i;

  g_return_val_if_fail (scan_hdr != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size > offset, FALSE);

  size -= offset;
  gst_byte_reader_init (&br, &data[offset], size);
  g_return_val_if_fail (size >= 3, FALSE);

  U_READ_UINT16 (&br, length);  /* Ls */
  g_return_val_if_fail (size >= length, FALSE);

  U_READ_UINT8 (&br, scan_hdr->num_components);
  g_return_val_if_fail (scan_hdr->num_components <=
      GST_JPEG_MAX_SCAN_COMPONENTS, FALSE);

  length -= 3;
  g_return_val_if_fail (length >= 2 * scan_hdr->num_components, FALSE);
  for (i = 0; i < scan_hdr->num_components; i++) {
    U_READ_UINT8 (&br, scan_hdr->components[i].component_selector);
    U_READ_UINT8 (&br, val);
    scan_hdr->components[i].dc_selector = (val >> 4) & 0x0F;
    scan_hdr->components[i].ac_selector = val & 0x0F;
    g_return_val_if_fail ((scan_hdr->components[i].dc_selector < 4 &&
            scan_hdr->components[i].ac_selector < 4), FALSE);
    length -= 2;
  }

  /* FIXME: Ss, Se, Ah, Al */
  g_assert (length == 3);
  return TRUE;
}

gboolean
gst_jpeg_parse_huffman_table (GstJpegHuffmanTables * huf_tables,
    const guint8 * data, gsize size, guint offset)
{
  GstByteReader br;
  GstJpegHuffmanTable *huf_table;
  guint16 length;
  guint8 val, table_class, table_index;
  guint32 value_count;
  guint i;

  g_return_val_if_fail (huf_tables != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size > offset, FALSE);

  size -= offset;
  gst_byte_reader_init (&br, &data[offset], size);
  g_return_val_if_fail (size >= 2, FALSE);

  U_READ_UINT16 (&br, length);  /* Lh */
  g_return_val_if_fail (size >= length, FALSE);

  while (gst_byte_reader_get_remaining (&br)) {
    U_READ_UINT8 (&br, val);
    table_class = ((val >> 4) & 0x0F);
    table_index = (val & 0x0F);
    g_return_val_if_fail (table_index < GST_JPEG_MAX_SCAN_COMPONENTS, FALSE);
    if (table_class == 0) {
      huf_table = &huf_tables->dc_tables[table_index];
    } else {
      huf_table = &huf_tables->ac_tables[table_index];
    }
    READ_BYTES (&br, huf_table->huf_bits, 16);
    value_count = 0;
    for (i = 0; i < 16; i++)
      value_count += huf_table->huf_bits[i];
    READ_BYTES (&br, huf_table->huf_values, value_count);
    huf_table->valid = TRUE;
  }
  return TRUE;

failed:
  return FALSE;
}

gboolean
gst_jpeg_parse_quant_table (GstJpegQuantTables * quant_tables,
    const guint8 * data, gsize size, guint offset)
{
  GstByteReader br;
  GstJpegQuantTable *quant_table;
  guint16 length;
  guint8 val, table_index;
  guint i;

  g_return_val_if_fail (quant_tables != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size > offset, FALSE);

  size -= offset;
  gst_byte_reader_init (&br, &data[offset], size);
  g_return_val_if_fail (size >= 2, FALSE);

  U_READ_UINT16 (&br, length);  /* Lq */
  g_return_val_if_fail (size >= length, FALSE);

  while (gst_byte_reader_get_remaining (&br)) {
    U_READ_UINT8 (&br, val);
    table_index = (val & 0x0f);
    g_return_val_if_fail (table_index < GST_JPEG_MAX_SCAN_COMPONENTS, FALSE);
    quant_table = &quant_tables->quant_tables[table_index];
    quant_table->quant_precision = ((val >> 4) & 0x0f);

    g_return_val_if_fail (gst_byte_reader_get_remaining (&br) >=
        GST_JPEG_MAX_QUANT_ELEMENTS * (1 + ! !quant_table->quant_precision),
        FALSE);
    for (i = 0; i < GST_JPEG_MAX_QUANT_ELEMENTS; i++) {
      if (!quant_table->quant_precision) {      /* 8-bit values */
        U_READ_UINT8 (&br, val);
        quant_table->quant_table[i] = val;
      } else {                  /* 16-bit values */
        U_READ_UINT16 (&br, quant_table->quant_table[i]);
      }
    }
    quant_table->valid = TRUE;
  }
  return TRUE;
}

gboolean
gst_jpeg_parse_restart_interval (guint * interval,
    const guint8 * data, gsize size, guint offset)
{
  GstByteReader br;
  guint16 length, val;

  g_return_val_if_fail (interval != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size > offset, FALSE);

  size -= offset;
  gst_byte_reader_init (&br, &data[offset], size);
  g_return_val_if_fail (size >= 4, FALSE);

  U_READ_UINT16 (&br, length);  /* Lr */
  g_return_val_if_fail (size >= length, FALSE);

  U_READ_UINT16 (&br, val);
  *interval = val;
  return TRUE;
}

void
gst_jpeg_get_default_huffman_tables (GstJpegHuffmanTables * huf_tables)
{
  g_assert (huf_tables);

  memcpy (huf_tables, &default_huf_tables, sizeof (*huf_tables));
}

void
gst_jpeg_get_default_quantization_tables (GstJpegQuantTables * quant_tables)
{
  g_assert (quant_tables);

  memcpy (quant_tables, &default_quant_tables_zigzag, sizeof (*quant_tables));
}

gboolean
gst_jpeg_parse (GstJpegMarkerSegment * seg,
    const guint8 * data, gsize size, guint offset)
{
  GstByteReader br;
  guint16 length;

  g_return_val_if_fail (seg != NULL, FALSE);

  if (size <= offset) {
    GST_DEBUG ("failed to parse from offset %u, buffer is too small", offset);
    return FALSE;
  }

  size -= offset;
  gst_byte_reader_init (&br, &data[offset], size);

  if (!jpeg_parse_to_next_marker (&br, &seg->marker)) {
    GST_DEBUG ("failed to find marker code");
    return FALSE;
  }

  seg->offset = offset + gst_byte_reader_get_pos (&br);
  seg->size = -1;

  /* Try to find end of segment */
  switch (seg->marker) {
    case GST_JPEG_MARKER_SOI:
    case GST_JPEG_MARKER_EOI:
    fixed_size_segment:
      seg->size = 2;
      break;

    case (GST_JPEG_MARKER_SOF_MIN + 0):        /* Lf */
    case (GST_JPEG_MARKER_SOF_MIN + 1):        /* Lf */
    case (GST_JPEG_MARKER_SOF_MIN + 2):        /* Lf */
    case (GST_JPEG_MARKER_SOF_MIN + 3):        /* Lf */
    case (GST_JPEG_MARKER_SOF_MIN + 9):        /* Lf */
    case (GST_JPEG_MARKER_SOF_MIN + 10):       /* Lf */
    case (GST_JPEG_MARKER_SOF_MIN + 11):       /* Lf */
    case GST_JPEG_MARKER_SOS:  /* Ls */
    case GST_JPEG_MARKER_DQT:  /* Lq */
    case GST_JPEG_MARKER_DHT:  /* Lh */
    case GST_JPEG_MARKER_DAC:  /* La */
    case GST_JPEG_MARKER_DRI:  /* Lr */
    case GST_JPEG_MARKER_COM:  /* Lc */
    case GST_JPEG_MARKER_DNL:  /* Ld */
    variable_size_segment:
      READ_UINT16 (&br, length);
      seg->size = length;
      break;

    default:
      /* Application data segment length (Lp) */
      if (seg->marker >= GST_JPEG_MARKER_APP_MIN &&
          seg->marker <= GST_JPEG_MARKER_APP_MAX)
        goto variable_size_segment;

      /* Restart markers (fixed size, two bytes only) */
      if (seg->marker >= GST_JPEG_MARKER_RST_MIN &&
          seg->marker <= GST_JPEG_MARKER_RST_MAX)
        goto fixed_size_segment;

      /* Fallback: scan for next marker */
      if (!jpeg_parse_to_next_marker (&br, NULL))
        goto failed;
      seg->size = gst_byte_reader_get_pos (&br) - seg->offset;
      break;
  }
  return TRUE;

failed:
  return FALSE;
}