/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "decode_simple.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/pixdesc.h"

#include "libavformat/avformat.h"

#include "libavcodec/avcodec.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

typedef struct PrivData {
    const char *filterchain;
    AVFilterGraph   *fg;
    AVFilterContext *src;
    AVFilterContext *sink;
    AVFrame         *frame;

    uint64_t nb_frames;
} PrivData;

static int process_frame(DecodeContext *dc, AVFrame *frame)
{
    PrivData *pd = dc->opaque;
    int ret;

    if (!pd->fg) {
        AVFilterInOut *inputs, *outputs;
        char filterchain[1024];

        if (!frame)
            return 0;

        snprintf(filterchain, sizeof(filterchain),
                 "buffer@src=width=%d:height=%d:pix_fmt=%s:time_base=%d/%d,"
                 "%s,buffersink@sink",
                 frame->width, frame->height,
                 av_get_pix_fmt_name(frame->format),
                 dc->stream->time_base.num, dc->stream->time_base.den,
                 pd->filterchain);

        pd->fg = avfilter_graph_alloc();
        if (!pd->fg)
            return AVERROR(ENOMEM);

        ret = avfilter_graph_parse2(pd->fg, filterchain, &inputs, &outputs);
        if (ret < 0)
            return ret;

        av_assert0(!inputs && !outputs);

        pd->src  = avfilter_graph_get_filter(pd->fg, "buffer@src");
        pd->sink = avfilter_graph_get_filter(pd->fg, "buffersink@sink");
        av_assert0(pd->src && pd->sink);

        ret = avfilter_graph_config(pd->fg, pd->fg);
        if (ret < 0)
            return ret;

        pd->frame = av_frame_alloc();
        if (!pd->frame)
            return AVERROR(ENOMEM);
    }

    ret = av_buffersrc_write_frame(pd->src, frame);
    if (ret < 0)
        return ret;

    while (ret >= 0) {
        AVDictionaryEntry *t = NULL;

        ret = av_buffersink_get_frame(pd->sink, pd->frame);
        if ((frame  && ret == AVERROR(EAGAIN)) ||
            (!frame && ret == AVERROR_EOF))
            return 0;
        else if (ret < 0)
            return ret;

        fprintf(stdout, "frame %"PRIu64"\n", pd->nb_frames++);
        while ((t = av_dict_get(pd->frame->metadata, "lavfi.ssim360", t, AV_DICT_IGNORE_SUFFIX)))
            fprintf(stdout, "%s=%s\n", t->key, t->value);
    }

    return 0;
}

int main(int argc, char **argv)
{
    PrivData      pd;
    DecodeContext dc;

    const char *filename, *fc;
    int ret = 0;

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <input file> <filterchain>\n", argv[0]);
        return 0;
    }

    filename = argv[1];
    fc       = argv[2];

    memset(&pd, 0, sizeof(pd));
    pd.filterchain = fc;

    ret = ds_open(&dc, filename, 0);
    if (ret < 0)
        goto finish;

    dc.process_frame = process_frame;
    dc.opaque        = &pd;

    ret = ds_run(&dc);

finish:
    avfilter_graph_free(&pd.fg);
    av_frame_free(&pd.frame);
    ds_free(&dc);
    return ret;
}
