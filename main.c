#include <stdio.h>
#include <pthread.h>
#include <gst/gst.h>
#include <unistd.h>

#define NUM_THREADS 3
#define PASS_BUFFERS 7

typedef struct {
    GstElement *pipeline;
    GstElement *mixer;
    GstElement *sink;
} PipelineData;

typedef struct {
    int thread_num;
    PipelineData *pipeline_data;
    GstElement *src;
    GstPad *srcpad;
    GstPad *currentMixerPad;
    int passed_buffers;
} ThreadData;

static GstPadProbeReturn event_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data) {
    ThreadData *thread_data = (ThreadData *)user_data;

    // let one event pass and then create a new mxier pad and release the old one:
    if (thread_data->passed_buffers < thread_data->thread_num * PASS_BUFFERS) {
        thread_data->passed_buffers++;
        return GST_PAD_PROBE_PASS;
    }

    // create a new mixer pad and release the old one:
    gst_element_release_request_pad(thread_data->pipeline_data->mixer, thread_data->currentMixerPad);
    gst_pad_unlink(thread_data->srcpad, thread_data->currentMixerPad);
    gst_object_unref(thread_data->currentMixerPad);

    thread_data->currentMixerPad = gst_element_request_pad_simple(thread_data->pipeline_data->mixer, "sink_%u");

    gst_pad_link(thread_data->srcpad, thread_data->currentMixerPad);

    thread_data->passed_buffers = 0;

    return GST_PAD_PROBE_PASS;
}

void *setup_thread(ThreadData *thread_data) {
    int thread_num = thread_data->thread_num;
    PipelineData *pipeline_data = thread_data->pipeline_data;
    gchar *src_name = g_strdup_printf("src_%d", thread_num);
    thread_data->src = gst_element_factory_make("audiotestsrc", src_name);
    g_free(src_name);

    gst_bin_add(GST_BIN(pipeline_data->pipeline), thread_data->src);

    thread_data->currentMixerPad = gst_element_request_pad_simple(pipeline_data->mixer, "sink_%u");

    thread_data->srcpad = gst_element_get_static_pad(thread_data->src, "src");

    gst_pad_link(thread_data->srcpad, thread_data->currentMixerPad);

    gst_pad_add_probe(thread_data->srcpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, event_probe_cb, thread_data, NULL);
}

void *debug_dump_loop(void *data) {
    PipelineData *pipeline_data = (PipelineData *)data;
    while (1) {
        gst_debug_bin_to_dot_file_with_ts(GST_BIN(pipeline_data->pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");

        sleep(1);
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    GstMessage *msg;
    GstBus *bus;

    // Initialize GStreamer
    gst_init(&argc, &argv);

    // Create elements
    PipelineData pipeline_data;
    pipeline_data.pipeline = gst_pipeline_new("pipeline");

    // call gst_element_factory_make_with_properties to create a audiomixer with force-live=true and min-upstream-latency=500µs
    // GstElement *
    // gst_element_factory_make_with_properties (const gchar * factoryname,
    //                                           guint n,
    //                                           const gchar ** names,
    //                                           const GValue * values)

    guint n = 2;
    const gchar *names[] = {"force-live", "min-upstream-latency"};
    GValue values[] = {G_VALUE_INIT, G_VALUE_INIT};
    g_value_init(&values[0], G_TYPE_BOOLEAN);
    g_value_set_boolean(&values[0], TRUE);

    g_value_init(&values[1], G_TYPE_INT64);
    // value is in nanoseconds
    g_value_set_int64(&values[1], 500000);

    pipeline_data.mixer = gst_element_factory_make_with_properties("audiomixer", n, names, values);
    pipeline_data.sink = gst_element_factory_make("fakesink", "sink");
    // pipeline_data.sink = gst_element_factory_make("autoaudiosink", "sink");

    // Add elements to the pipeline
    gst_bin_add_many(GST_BIN(pipeline_data.pipeline), pipeline_data.mixer, pipeline_data.sink, NULL);

    // Link elements in the pipeline
    gst_element_link_many(pipeline_data.mixer, pipeline_data.sink, NULL);

    bus = gst_element_get_bus (pipeline_data.pipeline);

    // Create threads
    // pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].thread_num = i;
        thread_data[i].pipeline_data = &pipeline_data;
        // if (pthread_create(&threads[i], NULL, setup_thread, (void *)&thread_data[i])) {
        //     fprintf(stderr, "Error creating thread\n");
        //     return 1;
        // }
        setup_thread(&thread_data[i]);
    }

    gst_element_set_state(pipeline_data.pipeline, GST_STATE_PLAYING);

    gst_element_get_state(pipeline_data.pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

    gst_debug_bin_to_dot_file(GST_BIN(pipeline_data.pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");

    // create debug dump thread
    pthread_t debug_dump_thread;
    if (pthread_create(&debug_dump_thread, NULL, debug_dump_loop, (void *)&pipeline_data)) {
        fprintf(stderr, "Error creating thread\n");
        return 1;
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    g_main_loop_run (loop);

    // Free resources
    gst_object_unref(pipeline_data.pipeline);
    return 0;
}
