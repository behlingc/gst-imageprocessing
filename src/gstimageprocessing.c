/*
 * GStreamer
 * Copyright (C) 2015 Carsten Behling <behlin_c@gmx.de>>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-imageprocessing
 *
 * A plugin for learning basic image processing.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 v4l2src device=/dev/video1 ! imageprocessing halftone=true ! xvimagesink 
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <stdlib.h>
#include <string.h>

#include "gstimageprocessing.h"

GST_DEBUG_CATEGORY_STATIC (gst_image_processing_debug);
#define GST_CAT_DEFAULT gst_image_processing_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_GRAYSCALE,
  PROP_HALFTONE,
  PROP_HISTEQ
};

/* the capabilities of the inputs and outputs.
 *
 */
static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "video/x-raw, format=(string)I420, width=(int)640, height=(int)480"
  )
);

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "video/x-raw, format=(string)I420, width=(int)640, height=(int)480"
  )
);

#define gst_image_processing_parent_class parent_class
G_DEFINE_TYPE (GstImageProcessing, gst_image_processing, GST_TYPE_BASE_TRANSFORM);

static void gst_image_processing_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_image_processing_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_image_processing_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_imageprocessing_set_caps (GstBaseTransform *trans, GstCaps *incaps,
                                   GstCaps *outcaps);

/* Image Processing algorithms */
static void half_tone(guchar *in_image, guchar *out_image, guchar threshold, 
          guchar one, guchar zero,  guint rows, guint cols);
static void hist_equalization(guchar *in_image, guchar *out_image, guint rows, guint cols);

/* GObject vmethod implementations */

/* initialize the imageprocessing's class */
static void
gst_image_processing_class_init (GstImageProcessingClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_image_processing_set_property;
  gobject_class->get_property = gst_image_processing_get_property;

  g_object_class_install_property (gobject_class, PROP_GRAYSCALE,
    g_param_spec_boolean ("grayscale", "Grayscale", "Display gray-scaled video.",
          FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_HALFTONE,
    g_param_spec_boolean ("halftone", "Halftone", "Display halftone video.",
          FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_HISTEQ,
    g_param_spec_boolean ("histeq", "Histeq", "Histogram equalized video.",
          FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  gst_element_class_set_details_simple (gstelement_class,
    "ImageProcessing",
    "Filter/Video",
    "Image processing plugin",
    "Carsten Behling <behlin_c@gmx.de>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  GST_BASE_TRANSFORM_CLASS (klass)->transform =
      GST_DEBUG_FUNCPTR (gst_image_processing_transform);
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps =
      GST_DEBUG_FUNCPTR (gst_imageprocessing_set_caps);

  /* debug category for fltering log messages
   *
   * FIXME:exchange the string 'Template imageprocessing' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_image_processing_debug, "imageprocessing", 0, "Imageprocessing plugin");
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_image_processing_init (GstImageProcessing *filter)
{
  filter->grayscale = FALSE;
  filter->halftone = FALSE;
  filter->histeq = FALSE;
}

static void
gst_image_processing_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstImageProcessing *filter = GST_IMAGEPROCESSING (object);

  switch (prop_id) {
    case PROP_GRAYSCALE:
      filter->grayscale = g_value_get_boolean (value);
      break;
    case PROP_HALFTONE:
      filter->halftone = g_value_get_boolean (value);
      break;
    case PROP_HISTEQ:
      filter->histeq = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_image_processing_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstImageProcessing *filter = GST_IMAGEPROCESSING (object);

  switch (prop_id) {
    case PROP_GRAYSCALE:
      g_value_set_boolean (value, filter->grayscale);
      break;
    case PROP_HALFTONE:
      g_value_set_boolean (value, filter->halftone);
      break;
    case PROP_HISTEQ:
      g_value_set_boolean (value, filter->histeq);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstBaseTransform vmethod implementations */

/* this function does the actual processing
 */
static GstFlowReturn
gst_image_processing_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstImageProcessing *filter = GST_IMAGEPROCESSING (base);
  GstMapInfo inmap, outmap;
  guchar *chroma_offset;

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (outbuf)))
    gst_object_sync_values (G_OBJECT (filter), GST_BUFFER_TIMESTAMP (outbuf));

  gst_buffer_map (inbuf, &inmap, GST_MAP_READ);
  gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE);

  if (filter->halftone == TRUE) {
    half_tone(inmap.data, outmap.data, 128, 255, 0,  filter->height,
        filter->width);
  } else if (filter->histeq == TRUE) {
    hist_equalization(inmap.data, outmap.data,filter->height, filter->width);
  } else {
    memcpy(outmap.data, inmap.data, inmap.size);
  }


  if (filter->grayscale == TRUE || filter->halftone == TRUE
      || filter->histeq == TRUE) {
    chroma_offset = (guint8 *)outmap.data + inmap.size * 2/3;
    memset((void*)chroma_offset, 128, inmap.size/3);
  }
  
  /* FIXME: do something interesting here.  This simply copies the source
   * to the destination. */

  gst_buffer_unmap (inbuf, &inmap);
  gst_buffer_unmap (outbuf, &outmap);

  return GST_FLOW_OK;
}

static gboolean
gst_imageprocessing_set_caps (GstBaseTransform *trans, GstCaps *incaps,
                                   GstCaps *outcaps)
{
  GstImageProcessing *filter = GST_IMAGEPROCESSING (trans);
  GstStructure *structure;
  gboolean ret = FALSE;

  g_return_val_if_fail (gst_caps_is_fixed (incaps), FALSE);
  filter->width = 0;
  filter->height = 0;
  structure = gst_caps_get_structure (incaps, 0);
  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height))
    ret = TRUE;

  return ret;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
imageprocessing_init (GstPlugin * imageprocessing)
{
  return gst_element_register (imageprocessing, "imageprocessing", GST_RANK_NONE,
      GST_TYPE_IMAGEPROCESSING);
}

/* Image Processing algorithms */
static void 
half_tone(guchar *in_image, guchar *out_image, guchar threshold, 
          guchar one, guchar zero,  guint rows, guint cols)
{
  float **eg, **ep;
  float c[2][3], sum_p, t;
  int i, j, m, n, xx, yy;

  c[0][0] = 0.0;
  c[0][1] = 0.2;
  c[0][2] = 0.0;
  c[1][0] = 0.6;
  c[1][1] = 0.1;
  c[1][2] = 0.1;

  eg = malloc(rows * sizeof(float  *));
  for(i=0; i<rows; i++){
    eg[i] = malloc(cols * sizeof(float ));
  }

  ep = malloc(rows * sizeof(float  *));
  for(i=0; i<rows; i++){
    ep[i] = malloc(cols * sizeof(float ));
  }

  for(i=0; i<rows; i++){
    for(j=0; j<cols; j++){
      eg[i][j] = 0.0;
      ep[i][j] = 0.0;
    }
  }

  for(m=0; m<rows; m++){
    for(n=0; n<cols; n++){
      sum_p = 0.0;
      for(i=0; i<2; i++){
        for(j=0; j<3; j++){
          xx = m-i+1;
          yy = n-j+1;
          if(xx < 0) xx = 0;
          if(xx >= rows) xx = rows-1;
          if(yy < 0)     yy = 0;
          if(yy >= cols) yy = cols-1;

          sum_p = sum_p + c[i][j] * eg[xx][yy];
        }
      }
      ep[m][n] = sum_p;
      t = in_image[m*cols+n] + ep[m][n];
      if(t > threshold){
        eg[m][n] = t - threshold*2;
        out_image[m*cols+n] = one;
      } else { /* t <= threshold */
        eg[m][n] = t;
        out_image[m*cols+n] = zero;
      }
    }
  }

  for(i=0; i<rows; i++){
    free(eg[i]);
    free(ep[i]);
  }
}

static void
hist_equalization(guchar *in_image, guchar *out_image, guint rows, guint cols)
{
  int m, n;
  guchar histogram[256];
  unsigned long sum, histogram_sum[256];
  double constant;

  /* calculate histogram */
  memset(histogram, 0, sizeof(histogram));
  for(m=0; m<rows; m++){
    for(n=0; n<cols; n++){
       histogram[in_image[m*cols+n]]++;
    }
  }
  /* calculate histogram sum */
  sum = 0;
  memset(histogram_sum, 0, sizeof(histogram_sum));
  for(m=0; m<256; m++){
    sum += histogram[m];
    histogram_sum[m] = sum;
    //printf("Sum: %ld\n", sum);
  }

  constant = 256.f/(double)(histogram_sum[255]);
  /* calculate output image */
  for(m=0; m<rows; m++){
    for(n=0; n<cols; n++) {
         out_image[m*cols+n] = histogram_sum[in_image[m*cols+n]] * constant;
    }
  }
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "gst-imageprocessing"
#endif

/* gstreamer looks for this structure to register imageprocessings
 *
 * FIXME:exchange the string 'Template imageprocessing' with you imageprocessing description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    imageprocessing,
    "Image processing plugin",
    imageprocessing_init,
    VERSION,
    "LGPL",
    "Carsten Behling",
    "https://github.com/behlingc/gst-imageprocessing"
)
