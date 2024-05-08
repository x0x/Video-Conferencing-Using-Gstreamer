#include <gst/gst.h>
#include <pthread.h> // Include pthread for threading

char dest_ip[10];
int dest_port = 7001;
int src_port = 7002;

typedef struct {
  GstElement *pipeline;
  GstElement *v4l2src;
  GstElement *videoconvert;
  GstElement *x264enc;
  GstElement *queue1;
  GstElement *mpegtsmux;
  GstElement *openalsrc;
  GstElement *audioconvert;
  GstElement *avenc_ac3;
  GstElement *queue2;
  GstElement *udpsink;
  GstElement *payloader;
} Sender;

typedef struct {
  GstElement *pipeline, *udp_src, *rtpmp2tdepay, *decodebin, *video_converter,
      *video_sink, *audio_sink, *audio_converter, *audio_resampler;
} Receiver;

static void pad_added_handler(GstElement *src, GstPad *pad, Receiver *receiver);

void setup_sender_pipeline(Sender *sender) {
  /* Initialize GStreamer */
  gst_init(NULL, NULL);

  /* Create elements */
  sender->pipeline = gst_pipeline_new("pipeline");
  sender->v4l2src = gst_element_factory_make("v4l2src", "v4l2src");
  sender->videoconvert =
      gst_element_factory_make("videoconvert", "videoconvert");
  sender->x264enc = gst_element_factory_make("x264enc", "x264enc");
  sender->queue1 = gst_element_factory_make("queue", "queue1");
  sender->mpegtsmux = gst_element_factory_make("mpegtsmux", "mpegtsmux");
  sender->openalsrc = gst_element_factory_make("openalsrc", "openalsrc");
  sender->audioconvert =
      gst_element_factory_make("audioconvert", "audioconvert");
  sender->avenc_ac3 = gst_element_factory_make("avenc_ac3", "avenc_ac3");
  sender->queue2 = gst_element_factory_make("queue", "queue2");
  sender->udpsink = gst_element_factory_make("udpsink", "udpsink");
  sender->payloader = gst_element_factory_make("rtpmp2tpay", "payloader");

  /* Check if elements are created successfully */
  if (!sender->pipeline || !sender->v4l2src || !sender->videoconvert ||
      !sender->x264enc || !sender->queue1 || !sender->mpegtsmux ||
      !sender->openalsrc || !sender->audioconvert || !sender->avenc_ac3 ||
      !sender->queue2 || !sender->udpsink || !sender->payloader) {
    g_printerr("One or more elements could not be created. Exiting.\n");
    exit(EXIT_FAILURE);
  }

  /* Set queue properties */
  g_object_set(G_OBJECT(sender->queue1), "max-size-time",
               G_GINT64_CONSTANT(10000000000), NULL);
  g_object_set(G_OBJECT(sender->queue2), "max-size-time",
               G_GINT64_CONSTANT(10000000000), NULL);

  /* Set UDP sink properties */
  g_object_set(G_OBJECT(sender->udpsink), "host", dest_ip, "port", dest_port, NULL);

  /* Add elements to the pipeline */
  gst_bin_add_many(GST_BIN(sender->pipeline), sender->v4l2src,
                   sender->videoconvert, sender->x264enc, sender->queue1,
                   sender->mpegtsmux, sender->openalsrc, sender->audioconvert,
                   sender->avenc_ac3, sender->queue2, sender->udpsink,
                   sender->payloader, NULL);

  /* Link video elements */
  if (!gst_element_link_many(sender->v4l2src, sender->videoconvert,
                             sender->x264enc, sender->queue1, sender->mpegtsmux,
                             NULL)) {
    g_printerr("Video elements could not be linked. Exiting.\n");
    exit(EXIT_FAILURE);
  }

  /* Link audio elements */
  if (!gst_element_link_many(sender->openalsrc, sender->audioconvert,
                             sender->avenc_ac3, sender->queue2,
                             sender->mpegtsmux, sender->payloader,
                             sender->udpsink, NULL)) {
    g_printerr("Audio elements could not be linked. Exiting.\n");
    exit(EXIT_FAILURE);
  }
}

void setup_receiver_pipeline(Receiver *receiver) {
  /* Initialize GStreamer */
  gst_init(NULL, NULL);

  /* Create pipeline */
  receiver->pipeline = gst_pipeline_new("receiver-pipeline");
  if (!receiver->pipeline) {
    g_printerr("Failed to create the pipeline.\n");
    exit(EXIT_FAILURE);
  }

  /* Create elements */
  receiver->udp_src = gst_element_factory_make("udpsrc", "udp-source");
  receiver->rtpmp2tdepay =
      gst_element_factory_make("rtpmp2tdepay", "rtp-mp2t-depay");
  receiver->decodebin = gst_element_factory_make("decodebin", "decoder");
  receiver->video_converter =
      gst_element_factory_make("videoconvert", "videoconvert");
  receiver->video_sink =
      gst_element_factory_make("autovideosink", "video_sink");
  receiver->audio_converter =
      gst_element_factory_make("audioconvert", "audioconvert");
  receiver->audio_resampler =
      gst_element_factory_make("audioresample", "audioresample");
  receiver->audio_sink =
      gst_element_factory_make("autoaudiosink", "autoaudiosink");

  /* Check element creation */
  if (!receiver->udp_src || !receiver->rtpmp2tdepay || !receiver->decodebin ||
      !receiver->video_converter || !receiver->video_sink ||
      !receiver->audio_sink || !receiver->audio_converter ||
      !receiver->audio_resampler) {
    g_printerr("Failed to create all elements.\n");
    exit(EXIT_FAILURE);
  }

  /* Set UDP source properties */
  g_object_set(receiver->udp_src, "port", src_port, "caps",
               gst_caps_from_string("application/x-rtp"), NULL);

  /* Add elements to the pipeline */
  gst_bin_add_many(GST_BIN(receiver->pipeline), receiver->udp_src,
                   receiver->rtpmp2tdepay, receiver->decodebin,
                   receiver->video_converter, receiver->video_sink,
                   receiver->audio_converter, receiver->audio_sink,
                   receiver->audio_resampler, NULL);

  /* Link elements */
  if (!gst_element_link_many(receiver->udp_src, receiver->rtpmp2tdepay,
                             receiver->decodebin, NULL) ||
      !gst_element_link(receiver->video_converter, receiver->video_sink) ||
      !gst_element_link_many(receiver->audio_converter,
                             receiver->audio_resampler, receiver->audio_sink,
                             NULL)) {
    g_printerr("Elements could not be linked.\n");
    exit(EXIT_FAILURE);
  }

  /* Connect pad-added signal */
  g_signal_connect(receiver->decodebin, "pad-added",
                   G_CALLBACK(pad_added_handler), receiver);

  g_print("Pipeline setup completed.\n");
}

void start_sender_pipeline(Sender *sender) {
  /* Set the pipeline to "playing" state */
  g_print("Sender Pipeline Started\n");
  GstStateChangeReturn ret =
      gst_element_set_state(sender->pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr(
        "Unable to set the sender pipeline to the playing state. Exiting.\n");
    exit(EXIT_FAILURE);
  }

  g_printerr("Stream started to ip %s at port %d\n",dest_ip,dest_port);
  /* Wait until error or EOS */
  GstBus *bus = gst_element_get_bus(sender->pipeline);
  GstMessage *msg = gst_bus_timed_pop_filtered(
      bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* Parse message */
  if (msg != NULL) {
    GError *err;
    gchar *debug_info;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error(msg, &err, &debug_info);
      g_printerr("Error received from element %s: %s\n",
                 GST_OBJECT_NAME(msg->src), err->message);
      g_printerr("Debugging information: %s\n",
                 debug_info ? debug_info : "none");
      g_clear_error(&err);
      g_free(debug_info);
      break;
    case GST_MESSAGE_EOS:
      g_print("End of stream reached.\n");
      break;
    default:
      g_printerr("Unexpected message received.\n");
      break;
    }

    gst_message_unref(msg);
  }

  /* Free resources */
  if (msg != NULL)
    gst_message_unref(msg);
  gst_object_unref(bus);
  gst_element_set_state(sender->pipeline, GST_STATE_NULL);
}

void start_receiver_pipeline(Receiver *receiver) {
  /* Set pipeline to playing state */
  GstStateChangeReturn ret =
      gst_element_set_state(receiver->pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr("Unable to start the receiver pipeline to the playing state.\n");
    exit(EXIT_FAILURE);
  }

  g_print("Receiver Pipeline set to playing state.\n");

  /* Wait until error or EOS */
  GstBus *bus = gst_element_get_bus(receiver->pipeline);
  GstMessage *msg = gst_bus_timed_pop_filtered(
      bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* Free resources */
  if (msg != NULL) {
    gst_message_unref(msg);
  }
  gst_object_unref(bus);
  gst_element_set_state(receiver->pipeline, GST_STATE_NULL);
  gst_object_unref(receiver->pipeline);

  g_print("Pipeline stopped and resources released.\n");
}

static void pad_added_handler(GstElement *src, GstPad *pad,
                              Receiver *receiver) {
  GstPad *video_sink_pad, *audio_sink_pad;
  GstCaps *caps = NULL;

  g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(pad),
          GST_ELEMENT_NAME(src));

  caps = gst_pad_get_current_caps(pad);
  if (!caps) {
    g_printerr("Failed to get caps.\n");
    return;
  }

  if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw"))) {
    g_print("Video pad added\n");

    video_sink_pad =
        gst_element_get_static_pad(receiver->video_converter, "sink");
    if (gst_pad_link(pad, video_sink_pad) != GST_PAD_LINK_OK) {
      g_printerr("Failed to link video elements.\n");
      return;
    }

    g_print("Video elements linked successfully.\n");
  } else if (gst_caps_is_subset(caps, gst_caps_from_string("audio/x-raw"))) {
    g_print("Audio pad added\n");

    audio_sink_pad =
        gst_element_get_static_pad(receiver->audio_converter, "sink");
    if (gst_pad_link(pad, audio_sink_pad) != GST_PAD_LINK_OK) {
      g_printerr("Failed to link audio elements.\n");
      return;
    }

    g_print("Audio elements linked successfully.\n");
  } else {
    g_printerr("Unknown pad type.\n");
  }
}

int main(int argc, char *argv[]) {

  if (argc != 4) {
    printf("Usage: %s <DEST_IP> <DEST_PORT> <SRC_PORT>\n", argv[0]);
    return 1; // Exit the program with an error code
  }

  strcpy(dest_ip, argv[1]);
  dest_port = atoi(argv[2]);
  src_port = atoi(argv[3]);
  Sender sender;
  Receiver receiver;
  /* Setup pipeline */
  setup_sender_pipeline(&sender);
  setup_receiver_pipeline(&receiver);
  /* Start pipeline */

  // Create threads for each pipeline
  pthread_t thread1, thread2;
  pthread_create(&thread1, NULL, start_sender_pipeline, &sender);
  pthread_create(&thread2, NULL, start_receiver_pipeline, &receiver);

  // Wait for threads to finish
  pthread_join(thread1, NULL);
  pthread_join(thread2, NULL);

  return 0;
}
