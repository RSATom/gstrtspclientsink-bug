#include <cassert>

#include <gst/gst.h>

#include <gst/rtsp-server/rtsp-server.h>

#define EMBEDDED_CLIENT 1

GST_DEBUG_CATEGORY_STATIC(bug_debug);
#define GST_CAT_DEFAULT bug_debug

#define DEFAULT_RTSP_PORT "8554"

static char *port = (char *) DEFAULT_RTSP_PORT;

static GstElement* Client = nullptr;
static guint Watchdog = 0;
volatile bool ClientRunning = false;

static void startClient(unsigned timeout)
{
    GST_INFO("Scheduling client start");

    g_timeout_add(timeout,
        [] (gpointer /*userData*/) -> gboolean {
            assert(!Client);

            Client = gst_parse_launch(
                "videotestsrc ! x264enc ! rtspclientsink debug=false location=rtsp://localhost:8554/test",
                nullptr);
            gst_element_set_name(Client, "!!!SENDER!!!");

            GST_INFO("Starting client...");
            gst_element_set_state(Client, GST_STATE_PLAYING);
            Watchdog =
                g_timeout_add_seconds(2,
                    [] (gpointer /*userData*/) -> gboolean {
                        GST_ERROR("BUG: Record not started!!! Dumping pipeline...");
                        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(Client), GST_DEBUG_GRAPH_SHOW_ALL, "test-record");
                        return FALSE;
                    }, nullptr);
            ClientRunning = true;

            return FALSE;
        }, nullptr
    );
}

static void restartClient()
{
    if(!ClientRunning)
        return;

    ClientRunning = FALSE;

    GST_INFO("Scheduling client restart");

    g_timeout_add(100,
        [] (gpointer /*userData*/) -> gboolean {
            GST_INFO("Stopping client...");
            gst_element_set_state(Client, GST_STATE_NULL);
            gst_object_unref(Client);
            Client = nullptr;

            g_source_remove(Watchdog);
            Watchdog = 0;

            startClient(500);

            return FALSE;
        }, nullptr
    );
}

static void
handoff(GstElement* fakesink, GstBuffer* buffer, GstPad* pad, gpointer /*userData*/)
{
#ifdef EMBEDDED_CLIENT
    if(ClientRunning)
        GST_INFO("Got buffer");

    restartClient();
#else
    GST_INFO("Got buffer");
#endif
}

static void
media_configure(
    GstRTSPMediaFactory* mediaFactory,
    GstRTSPMedia* media,
    gpointer /*userData*/)
{
    GST_INFO(">> media_configure");

    GstBin* bin = GST_BIN(gst_rtsp_media_get_element(media));
    GstElement* sink = gst_bin_get_by_name(bin, "sink");

    g_object_set(sink, "signal-handoffs", TRUE, NULL);

    g_signal_connect(sink, "handoff", G_CALLBACK(handoff), nullptr);

    g_object_unref(sink);
    g_object_unref(bin);
}

int
main (int argc, char *argv[])
{
    gst_init(0 , 0);

    GST_DEBUG_CATEGORY_INIT (bug_debug, "bug", 0, "bug demo app");

    GMainLoop* loop = g_main_loop_new (NULL, FALSE);

    GstRTSPServer* server = gst_rtsp_server_new ();

    g_object_set (server, "service", port, NULL);

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points (server);

    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new ();
    gst_rtsp_media_factory_set_transport_mode (factory, GST_RTSP_TRANSPORT_MODE_RECORD);
    gst_rtsp_media_factory_set_launch (factory,
      "rtph264depay name=depay0 ! identity name=identity ! fakesink name=sink");

    g_signal_connect(factory, "media-configure", G_CALLBACK(media_configure), nullptr);

    gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

    g_object_unref (mounts);

    gst_rtsp_server_attach (server, NULL);

    g_print ("stream ready at rtsp://127.0.0.1:%s/test\n", port);

#ifdef EMBEDDED_CLIENT
     startClient(100);
#endif

    g_main_loop_run (loop);

    return 0;
}
