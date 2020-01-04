#include <string>
#include <algorithm>
#include <functional>
#include <csignal>
#include "head.hpp"
#include "scoped_exit.hpp"

struct FilteringContext
{
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;
};

struct StreamContext
{
    int a_stream_index = -1;
    int v_stream_index = -1;
    std::string filename;

    AVCodec *v_codec = nullptr;
    AVCodec *a_codec = nullptr;

    AVStream *v_stream = nullptr;
    AVStream *a_stream = nullptr;

    AVCodecContext *v_codec_ctx = nullptr;
    AVCodecContext *a_codec_ctx = nullptr;

    AVFormatContext *fmt_ctx = nullptr;
    FilteringContext *filter_ctx;
    AVAudioFifo *fifo;
    int64_t audio_pts = 0;
    uint64_t audio_count = 0;
    uint64_t video_count = 0;
    uint64_t filter_video_count = 0;
    FILE *fp = nullptr;
    FILE *pkt_fp = nullptr;
};

typedef void (*encode_frame_fun)(StreamContext *, StreamContext *, AVFrame *);

static void destory_frame(AVFrame *frame)
{
    if (frame)
    {
        av_frame_free(&frame);
    }
}

static void destory_codec_context(AVCodecContext *ctx)
{
    if (ctx)
    {
        avcodec_free_context(&ctx);
    }
}

static void destory_filter_context(const StreamContext &in, const StreamContext &out)
{
    AVFormatContext *ifmt_ctx = in.fmt_ctx;
    AVFormatContext *ofmt_ctx = out.fmt_ctx;

    for (int i = 0; i < ifmt_ctx->nb_streams; i++)
    {
        if (out.filter_ctx && out.filter_ctx[i].filter_graph)
            avfilter_graph_free(&out.filter_ctx[i].filter_graph);
    }
    av_free(out.filter_ctx);
}

static void destory_in_context(StreamContext *ctx)
{
    destory_codec_context(ctx->a_codec_ctx);
    destory_codec_context(ctx->v_codec_ctx);
    avformat_close_input(&ctx->fmt_ctx);
    avformat_free_context(ctx->fmt_ctx);
}

static void destory_out_context(StreamContext *ctx)
{
    destory_codec_context(ctx->a_codec_ctx);
    destory_codec_context(ctx->v_codec_ctx);

    //close output
    avformat_close_input(&ctx->fmt_ctx);
    if (ctx->fmt_ctx && !(ctx->fmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ctx->fmt_ctx->pb);
    avformat_free_context(ctx->fmt_ctx);
}

static void write_yuv_frame_to_file(AVFrame *frame, FILE *fp)
{
    assert(frame);
    assert(fp);
    int y_size = frame->width * frame->height;
    fwrite(frame->data[0], 1, y_size, fp);      // Y
    fwrite(frame->data[1], 1, y_size / 4, fp);  // U
    fwrite(frame->data[2], 1, y_size / 4, fp);  // V
}

static void init_filter(FilteringContext *fctx, AVCodecContext *dec_ctx, AVCodecContext *enc_ctx, const char *filter_spec)
{

    const AVFilter *buffersrc = NULL;
    const AVFilter *buffersink = NULL;
    AVFilterContext *buffersrc_ctx = NULL;
    AVFilterContext *buffersink_ctx = NULL;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    AVFilterGraph *filter_graph = avfilter_graph_alloc();

    assert(outputs && inputs && filter_graph);

    auto puts_exit = make_scoped_exit([&]() {
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
    });

    int ret = 0;
    char args[512] = {0};
    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        buffersrc = avfilter_get_by_name("buffer");
        buffersink = avfilter_get_by_name("buffersink");
        assert(buffersrc && buffersink);

        snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d", dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt, dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);
        printf("%s\n", args);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
        assert(ret >= 0);

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
        assert(ret >= 0);
        ret = av_opt_set_bin(buffersink_ctx, "pix_fmts", (uint8_t *)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt), AV_OPT_SEARCH_CHILDREN);
        assert(ret >= 0);
    }
    else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        assert(buffersrc && buffersink);
        if (!dec_ctx->channel_layout)
        {
            dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);
        }
        snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64, dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate, av_get_sample_fmt_name(dec_ctx->sample_fmt), dec_ctx->channel_layout);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
        assert(ret >= 0);
        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
        assert(ret >= 0);
        ret = av_opt_set_bin(buffersink_ctx, "sample_fmts", (uint8_t *)&enc_ctx->sample_fmt, sizeof(enc_ctx->sample_fmt), AV_OPT_SEARCH_CHILDREN);
        assert(ret >= 0);
        ret = av_opt_set_bin(buffersink_ctx, "channel_layouts", (uint8_t *)&enc_ctx->channel_layout, sizeof(enc_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN);
        assert(ret >= 0);
        ret = av_opt_set_bin(buffersink_ctx, "sample_rates", (uint8_t *)&enc_ctx->sample_rate, sizeof(enc_ctx->sample_rate), AV_OPT_SEARCH_CHILDREN);
        assert(ret >= 0);
    }
    else
    {
        assert(false);
        return;
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;
    assert(outputs->name && inputs->name);
    ret = avfilter_graph_parse_ptr(filter_graph, filter_spec, &inputs, &outputs, NULL);
    assert(ret >= 0);
    ret = avfilter_graph_config(filter_graph, NULL);
    assert(ret >= 0);

    fctx->buffersrc_ctx = buffersrc_ctx;
    fctx->buffersink_ctx = buffersink_ctx;
    fctx->filter_graph = filter_graph;
}

static void init_filter_ctx(StreamContext *in, StreamContext *out)
{
    AVFormatContext *ifmt_ctx = in->fmt_ctx;
    FilteringContext *filter_ctx = nullptr;
    out->filter_ctx = (FilteringContext *)av_malloc_array(in->fmt_ctx->nb_streams, sizeof(*filter_ctx));
    filter_ctx = out->filter_ctx;
    assert(filter_ctx);
    for (int i = 0; i < in->fmt_ctx->nb_streams; i++)
    {
        filter_ctx[i].buffersrc_ctx = NULL;
        filter_ctx[i].buffersink_ctx = NULL;
        filter_ctx[i].filter_graph = NULL;

        if (i == in->v_stream_index)
        {
            //const char *filter_spec = " drawbox=:200:200:60:red@0.5:t=1"; [> passthrough (dummy) filter for video <]
            const char *filter_spec = "null";
            init_filter(&filter_ctx[i], in->v_codec_ctx, out->v_codec_ctx, filter_spec);
        }
        else if (i == in->a_stream_index)
        {
            printf(" audio filter index %d\n", i);
            const char *filter_spec = "anull"; /* passthrough (dummy) filter for audio */
            init_filter(&filter_ctx[i], in->a_codec_ctx, out->a_codec_ctx, filter_spec);
        }
    }
}

static void open_decoder_codec(StreamContext *ctx, int stream_index, AVCodec *codec)

{
    AVStream *stream = ctx->fmt_ctx->streams[stream_index];
    AVCodecParameters *cpr = stream->codecpar;
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    assert(codec_ctx);
    int status = avcodec_parameters_to_context(codec_ctx, cpr);
    assert(status >= 0);
    if (codec->type == AVMEDIA_TYPE_VIDEO)
    {
        codec_ctx->framerate = av_guess_frame_rate(ctx->fmt_ctx, stream, NULL);
    }
    status = avcodec_open2(codec_ctx, codec, NULL);
    assert(status == 0);
    if (codec->type == AVMEDIA_TYPE_AUDIO)
    {
        ctx->a_codec_ctx = codec_ctx;
        ctx->a_codec = codec;
        ctx->a_stream = stream;
        ctx->a_stream_index = stream_index;
        ctx->a_codec_ctx->channel_layout = av_get_default_channel_layout(ctx->a_codec_ctx->channels);
    }
    else if (codec->type == AVMEDIA_TYPE_VIDEO)
    {
        ctx->v_codec_ctx = codec_ctx;
        ctx->v_codec = codec;
        ctx->v_stream = stream;
        ctx->v_stream_index = stream_index;
    }
    else
    {
        assert(false);
    }
}

static void open_encoder_codec(StreamContext *in, StreamContext *out, AVCodec **codec, enum AVCodecID codec_id)
{
    AVFormatContext *oc = out->fmt_ctx;

    *codec = avcodec_find_encoder(codec_id);
    assert(codec);
    assert(*codec);
    AVStream *stream = avformat_new_stream(oc, NULL);
    assert(stream);
    int stream_index = oc->nb_streams - 1;

    AVCodecContext *c = avcodec_alloc_context3(*codec);
    assert(c);

    int status = 0;

    switch ((*codec)->type)
    {
        case AVMEDIA_TYPE_AUDIO:
            c->sample_rate = in->a_codec_ctx->sample_rate;
            c->channel_layout = in->a_codec_ctx->channel_layout;
            c->channels = av_get_channel_layout_nb_channels(c->channel_layout);
            c->sample_fmt = (*codec)->sample_fmts[0];
            c->time_base = (AVRational) {1, c->sample_rate};
            c->bit_rate = in->a_codec_ctx->bit_rate;
            out->a_codec = *codec;
            out->a_stream = stream;
            out->a_stream_index = stream_index;
            out->a_stream->time_base = c->time_base;
            out->a_codec_ctx = c;
            break;

        case AVMEDIA_TYPE_VIDEO:
            c->codec_id = codec_id;
            c->width = in->v_codec_ctx->width;
            c->height = in->v_codec_ctx->height;
            c->sample_aspect_ratio = in->v_codec_ctx->sample_aspect_ratio;
            c->time_base = av_inv_q(in->v_codec_ctx->framerate);
            if ((*codec)->pix_fmts)
                c->pix_fmt = (*codec)->pix_fmts[0];
            else
                c->pix_fmt = in->v_codec_ctx->pix_fmt;
            out->v_stream_index = stream_index;
            out->v_stream = stream;
            out->v_stream->time_base = c->time_base;
            out->v_codec = *codec;
            out->v_codec_ctx = c;
            break;

        default:
            break;
    }

    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
    {
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    status = avcodec_open2(c, *codec, nullptr);
    assert(status >= 0);
    status = avcodec_parameters_from_context(stream->codecpar, c);
    assert(status >= 0);
    if ((*codec)->type)
    {
        printf("audio smaple fmt %s %d %d\n", av_get_sample_fmt_name(c->sample_fmt), c->channels, c->frame_size);
        out->fifo = av_audio_fifo_alloc(c->sample_fmt, c->channels, c->frame_size);
        assert(out->fifo);
    }
}

void init_decoder(StreamContext *in)
{
    AVFormatContext *in_fmt_ctx = avformat_alloc_context();

    int status = avformat_open_input(&in_fmt_ctx, in->filename.data(), nullptr, nullptr);
    assert(status == 0);

    status = avformat_find_stream_info(in_fmt_ctx, nullptr);
    assert(status >= 0);

    av_dump_format(in_fmt_ctx, 0, in->filename.data(), 0);

    in->fmt_ctx = in_fmt_ctx;

    AVCodec *a_codec = nullptr;
    AVCodec *v_codec = nullptr;
    AVCodecContext *in_codec_ctx = nullptr;
    int v_stream_index = av_find_best_stream(in_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &v_codec, 0);
    int a_stream_index = av_find_best_stream(in_fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &a_codec, 0);
    assert(v_stream_index >= 0 && a_stream_index >= 0);
    assert(a_codec && v_codec);
    open_decoder_codec(in, v_stream_index, v_codec);
    open_decoder_codec(in, a_stream_index, a_codec);
    in->v_stream_index = v_stream_index;
    in->a_stream_index = a_stream_index;
}

void init_encoder(StreamContext *in, StreamContext *out)
{
    AVFormatContext *oc = nullptr;
    avformat_alloc_output_context2(&oc, NULL, NULL, out->filename.data());
    AVOutputFormat *fmt = oc->oformat;
    AVCodec *audio_codec = nullptr, *video_codec = nullptr;
    out->fmt_ctx = oc;

    if (fmt->video_codec != AV_CODEC_ID_NONE)
    {
        //open_encoder_codec(in, out, &video_codec, in->v_codec_ctx->codec_id);
        open_encoder_codec(in, out, &video_codec, fmt->video_codec);
    }
    if (fmt->audio_codec != AV_CODEC_ID_NONE)
    {
        open_encoder_codec(in, out, &audio_codec, fmt->audio_codec);
    }

    av_dump_format(oc, 0, out->filename.data(), 1);

    if (!(fmt->flags & AVFMT_NOFILE))
    {
        int status = avio_open(&oc->pb, out->filename.data(), AVIO_FLAG_WRITE);
        assert(status >= 0);
    }
    assert(avformat_write_header(oc, nullptr) >= 0);
}

static int init_output_frame(AVFrame **frame, AVCodecContext *c, int frame_size)
{
    if (!(*frame = av_frame_alloc()))
    {
        return -1;
    }

    (*frame)->nb_samples = frame_size;
    (*frame)->channel_layout = c->channel_layout;
    (*frame)->format = c->sample_fmt;
    (*frame)->sample_rate = c->sample_rate;

    int error = av_frame_get_buffer(*frame, 0);
    if (error < 0)
    {
        av_frame_free(frame);
        return -1;
    }

    return 0;
}

static int add_samples_to_fifo(AVAudioFifo *fifo,
                               uint8_t **converted_input_samples,
                               const int frame_size)
{
    int error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + frame_size);
    if (error < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Could not reallocate FIFO\n");
        return error;
    }

    if (av_audio_fifo_write(fifo, (void **)converted_input_samples, frame_size) < frame_size)
    {
        av_log(NULL, AV_LOG_ERROR, "Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }
    return 0;
}

static void WritePacketToFile(AVPacket *pkt, FILE *fp)
{
    assert(pkt);
    assert(fp);
    fwrite(pkt->data, 1, pkt->size, fp);
}

static int write_frame_file(StreamContext *in, StreamContext *out, int stream_index, AVFrame *frame)
{
    AVStream *in_stream = nullptr;
    AVStream *out_stream = nullptr;
    AVCodecContext *in_codec_ctx = nullptr;
    AVCodecContext *out_codec_ctx = nullptr;
    if (stream_index == in->a_stream_index)
    {
        in_stream = in->a_stream;
        out_stream = out->a_stream;
        in_codec_ctx = in->a_codec_ctx;
        out_codec_ctx = out->a_codec_ctx;
        if (frame)
        {
            frame->pts = out->audio_pts;
            out->audio_pts += frame->nb_samples;
        }
    }
    else if (stream_index == in->v_stream_index)
    {
        in_stream = in->v_stream;
        out_stream = out->v_stream;
        in_codec_ctx = in->v_codec_ctx;
        out_codec_ctx = out->v_codec_ctx;
    }
    else
    {
        assert(false);
        return 0;
    }
    int status = avcodec_send_frame(out_codec_ctx, frame);
    if (status < 0)
    {
        return status;
    }

    AVPacket pkt;
    pkt.data = nullptr;
    pkt.size = 0;
    av_init_packet(&pkt);
    AVPacket *packet = &pkt;
    assert(packet);
    //auto pkt_exit = make_scoped_exit([&]() { av_packet_free(&packet); });

    while (true)
    {
        status = avcodec_receive_packet(out_codec_ctx, packet);
        if (status < 0)
        {
            break;
        }
        auto pkt_unref = make_scoped_exit([&]() { av_packet_unref(packet); });
        packet->stream_index = stream_index;
        av_packet_rescale_ts(packet, in_codec_ctx->time_base, out_codec_ctx->time_base);
        av_packet_rescale_ts(packet, out_codec_ctx->time_base, out_stream->time_base);

        if (stream_index == in->a_stream_index)
        {
            printf("\t\t\t\t\t\t\t\t\tpts %07ld dts %07ld duration %07ld %07ld \t%s\n", packet->pts, packet->dts, packet->duration, ++out->audio_count, av_get_media_type_string(out_codec_ctx->codec_type));
        }
        else if (stream_index == in->v_stream_index)
        {
            printf("pts %07ld dts %07ld duration %07ld %07ld \t%s\n", packet->pts, packet->dts, packet->duration, ++out->video_count, av_get_media_type_string(out_codec_ctx->codec_type));
        }

        WritePacketToFile(packet, out->pkt_fp);
        int ret = av_interleaved_write_frame(out->fmt_ctx, packet);
        if (ret != 0)
        {
            printf("av_interleaved_write_frame failed\n");
        }
    }
    return 0;
}

void decoding_audio_packet_write_frame(StreamContext *in, StreamContext *out, AVFrame *f)
{

    int status = av_buffersrc_add_frame_flags(out->filter_ctx[in->a_stream_index].buffersrc_ctx, f, 0);
    assert(status >= 0);

    while (1)
    {
        AVFrame *filter_frame = av_frame_alloc();
        assert(filter_frame);
        auto frame_exit = make_scoped_exit([&]() { av_frame_free(&filter_frame); });

        status = av_buffersink_get_frame(out->filter_ctx[in->a_stream_index].buffersink_ctx, filter_frame);
        if (status < 0)
        {
            break;
        }

        assert(status >= 0);

        filter_frame->pict_type = AV_PICTURE_TYPE_NONE;

        AVCodecContext *enc_ctx = out->a_codec_ctx;
        const int output_frame_size = enc_ctx->frame_size;

        add_samples_to_fifo(out->fifo, filter_frame->data, filter_frame->nb_samples);
        int audio_fifo_size = av_audio_fifo_size(out->fifo);
        if (audio_fifo_size < enc_ctx->frame_size)
        {
            return;
        }

        while (av_audio_fifo_size(out->fifo) >= enc_ctx->frame_size)
        {
            const int frame_size = FFMIN(av_audio_fifo_size(out->fifo), enc_ctx->frame_size);
            AVFrame *frame;
            if (init_output_frame(&frame, enc_ctx, frame_size) < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "init_output_frame failed\n");
                break;
            }
            auto frame_exit = make_scoped_exit([&]() { av_frame_free(&frame); });
            if (av_audio_fifo_read(out->fifo, (void **)frame->data, frame_size) < frame_size)
            {
                av_log(NULL, AV_LOG_ERROR, "Could not read data from FIFO\n");
                av_frame_free(&frame);
                break;
            }
            write_frame_file(in, out, in->a_stream_index, frame);
        }
    }
}

void decoding_video_packet_write_frame(StreamContext *in, StreamContext *out, AVFrame *f)
{
    int stream_index = in->v_stream_index;
    assert(out->filter_ctx[stream_index].buffersrc_ctx);
    int status = av_buffersrc_add_frame_flags(out->filter_ctx[stream_index].buffersrc_ctx, f, 0);
    assert(status >= 0);
    while (true)
    {
        AVFrame *filter_frame = av_frame_alloc();
        assert(filter_frame);
        assert(out->filter_ctx->buffersink_ctx);
        auto frame_exit = make_scoped_exit([&]() { av_frame_free(&filter_frame); });
        //printf("%p video stream_index %d\n", out->filter_ctx[stream_index].buffersink_ctx, stream_index);
        status = av_buffersink_get_frame(out->filter_ctx[stream_index].buffersink_ctx, filter_frame);
        if (status < 0)
        {
            break;
        }
        auto frame_unref = make_scoped_exit([&]() { av_frame_unref(filter_frame); });
        write_yuv_frame_to_file(filter_frame, out->fp);
        filter_frame->pict_type = AV_PICTURE_TYPE_NONE;
        write_frame_file(in, out, in->v_stream_index, filter_frame);
    }
}

void decoding_packet_write_packet(StreamContext *in, StreamContext *out, AVPacket *pkt)
{
    int stream_index = pkt->stream_index;
    AVCodecContext *in_ctx = nullptr;
    encode_frame_fun encodec_call = nullptr;
    if (stream_index == in->a_stream_index)
    {
        in_ctx = in->a_codec_ctx;
        encodec_call = decoding_audio_packet_write_frame;
    }
    else if (stream_index == in->v_stream_index)
    {
        in_ctx = in->v_codec_ctx;
        encodec_call = decoding_video_packet_write_frame;
    }
    AVStream *stream = in->fmt_ctx->streams[stream_index];
    av_packet_rescale_ts(pkt, stream->time_base, in_ctx->time_base);

    int status = avcodec_send_packet(in_ctx, pkt);
    if (status < 0)
    {
        return;
    }
    AVFrame *frame = av_frame_alloc();
    auto frame_exit = make_scoped_exit([&]() { av_frame_free(&frame); });
    while (true)
    {
        status = avcodec_receive_frame(in_ctx, frame);
        if (status < 0)
        {
            break;
        }
        auto frame_exit2 = make_scoped_exit([&]() { av_frame_unref(frame); });
        //frame->pts = av_rescale_q(pkt->pts, in_ctx->time_base, stream->time_base);

        frame->pts = frame->best_effort_timestamp;
        encodec_call(in, out, frame);
    }
}

void flush_stream_context(StreamContext *in, StreamContext *out)
{
    AVPacket pkt;
    pkt.stream_index = in->a_stream_index;
    pkt.size = 0;
    pkt.data = nullptr;
    decoding_packet_write_packet(in, out, &pkt);
    pkt.stream_index = in->v_stream_index;
    decoding_packet_write_packet(in, out, &pkt);
    write_frame_file(in, out, in->v_stream_index, nullptr);
}

namespace
{
std::function<void(int)> shutdown_handler;
void signal_handler(int signal) { shutdown_handler(signal); }
}  // namespace

int main(int argc, char *argv[])
{
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGKILL, signal_handler);
    std::signal(SIGINT, signal_handler);
    bool runing = false;
    shutdown_handler = [&](int signal) { runing = false; };

    if (argc < 3)
    {
        printf("usage %s inputfile outputfile\n", argv[0]);
        return -1;
    }
    StreamContext in;
    StreamContext out;
    in.filename = argv[1];
    out.filename = argv[2];
    std::string parting_line(50, '-');
    //
    init_decoder(&in);
    auto in_exit = make_scoped_exit([&]() { destory_in_context(&in); });

    init_encoder(&in, &out);
    auto out_exit = make_scoped_exit([&]() { destory_out_context(&out); });

    init_filter_ctx(&in, &out);
    auto filter_exit = make_scoped_exit([&]() { destory_filter_context(in, out); });
    //
    printf("%s\n", parting_line.data());
    // test only
    out.fp = fopen("test.yuv", "wb");
    out.pkt_fp = fopen("pkt.h264", "wb");
    assert(out.fp && out.pkt_fp);
    auto fp_exit = make_scoped_exit([&]() { fclose(out.fp); });
    auto pkt_fp_exit = make_scoped_exit([&]() { fclose(out.pkt_fp); });

    runing = true;
    AVPacket packet;
    AVPacket *pkt = &packet;
    uint64_t audio_count = 0;
    uint64_t video_count = 0;
    while (runing)
    {
        int status = av_read_frame(in.fmt_ctx, pkt);
        if (status < 0)
        {
            break;
        }
        auto pkt_exit = make_scoped_exit([&]() { av_packet_unref(pkt); });
        if (pkt->stream_index == in.a_stream_index)
        {
            //printf("audio packet %ld\n", ++audio_count);
            //decoding_packet_write_packet(&in, &out, pkt);
        }
        else if (pkt->stream_index == in.v_stream_index)
        {
            printf("video packet %ld\n", ++video_count);
            decoding_packet_write_packet(&in, &out, pkt);
        }
        else
        {
            continue;
        }
    }
    av_write_trailer(out.fmt_ctx);
    printf("read packet %ld write packet %ld filter count %ld\n", video_count, out.video_count, out.filter_video_count);
    return 0;
}
