#include <stdio.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <boost/thread.hpp>

#include <ros/ros.h>

#include "audio_common_msgs/AudioData.h"
#include "audio_common_msgs/AudioDataStamped.h"
#include "audio_common_msgs/AudioInfo.h"

std::vector<uint8_t> _data_buffer;
int _data_buffer_length;

namespace audio_transport
{
  class RosGstCapture
  {
    public:
      RosGstCapture()
      {
        _bitrate = 192;
        
        std::string dst_type;

        // Need to encoding or publish raw wave data
        ros::param::param<std::string>("~format", _format, "mp3");
        ros::param::param<std::string>("~sample_format", _sample_format, "S16LE");

        // The bitrate at which to encode the audio
        ros::param::param<int>("~bitrate", _bitrate, 192);

        // only available for raw data
        ros::param::param<int>("~channels", _channels, 1);
        ros::param::param<int>("~depth", _depth, 16);
        ros::param::param<int>("~sample_rate", _sample_rate, 16000);

        // The destination of the audio
        ros::param::param<std::string>("~dst", dst_type, "appsink");

        // The source of the audio
        //ros::param::param<std::string>("~src", source_type, "alsasrc");
        std::string device;
        ros::param::param<std::string>("~device", device, "");

        // wjc added
        ros::param::param<int>("~pub_frequency", _pub_frequency, 60);
        _data_buffer_length = 1.0 / _pub_frequency * _sample_rate * _channels * 2;

        _pub = _nh.advertise<audio_common_msgs::AudioData>("audio", 10, true);
        _pub_stamped = _nh.advertise<audio_common_msgs::AudioDataStamped>("audio_stamped", 10, true);
        _pub_info = _nh.advertise<audio_common_msgs::AudioInfo>("audio_info", 1, true);

        _loop = g_main_loop_new(NULL, false);
        _pipeline = gst_pipeline_new("ros_pipeline");
        GstClock *clock = gst_system_clock_obtain();
        g_object_set(clock, "clock-type", GST_CLOCK_TYPE_REALTIME, NULL);
        gst_pipeline_use_clock(GST_PIPELINE_CAST(_pipeline), clock);
        gst_object_unref(clock);

        _bus = gst_pipeline_get_bus(GST_PIPELINE(_pipeline));
        gst_bus_add_signal_watch(_bus);
        g_signal_connect(_bus, "message::error",
                         G_CALLBACK(onMessage), this);
        g_object_unref(_bus);

        // We create the sink first, just for convenience
        if (dst_type == "appsink")
        {
          _sink = gst_element_factory_make("appsink", "sink");
          g_object_set(G_OBJECT(_sink), "emit-signals", true, NULL);
          g_object_set(G_OBJECT(_sink), "max-buffers", 100, NULL);

          // //控制发布频率 *** 这个ChatGPT搜出来的方法不行
          // float pub_frequency = 1;
          // float blocksize_float = 1 / pub_frequency * _sample_rate * _channels * 2;
          // int blocksize = static_cast<int>(blocksize_float);
          // g_object_set(G_OBJECT(_sink), "blocksize", blocksize, NULL);
          // gint current_blocksize;
          // g_object_get(G_OBJECT(_sink), "blocksize", &current_blocksize, NULL);
          // ROS_INFO_STREAM("Blocksize: " << current_blocksize);
          
          g_signal_connect( G_OBJECT(_sink), "new-sample",
                            G_CALLBACK(onNewBuffer), this);
        }
        else
        {
          ROS_INFO("file sink to %s", dst_type.c_str());
          _sink = gst_element_factory_make("filesink", "sink");
          g_object_set( G_OBJECT(_sink), "location", dst_type.c_str(), NULL);
        }

        _source = gst_element_factory_make("alsasrc", "source");
        // if device isn't specified, it will use the default which is
        // the alsa default source.
        // A valid device will be of the foram hw:0,0 with other numbers
        // than 0 and 0 as are available.
        if (device != "")
        {
          // ghcar *gst_device = device.c_str();
          g_object_set(G_OBJECT(_source), "device", device.c_str(), NULL);
        }

        GstCaps *caps;
        caps = gst_caps_new_simple("audio/x-raw",
                                   "format", G_TYPE_STRING, _sample_format.c_str(),
                                   "channels", G_TYPE_INT, _channels,
                                   "width",    G_TYPE_INT, _depth,
                                   "depth",    G_TYPE_INT, _depth,
                                   "rate",     G_TYPE_INT, _sample_rate,
                                   "signed",   G_TYPE_BOOLEAN, TRUE,
                                   NULL);

        gboolean link_ok;
        if (_format == "mp3"){
          _filter = gst_element_factory_make("capsfilter", "filter");
          g_object_set( G_OBJECT(_filter), "caps", caps, NULL);
          gst_caps_unref(caps);

          _convert = gst_element_factory_make("audioconvert", "convert");
          if (!_convert) {
            ROS_ERROR_STREAM("Failed to create audioconvert element");
            exitOnMainThread(1);
          }

          _encode = gst_element_factory_make("lamemp3enc", "encoder");
          if (!_encode) {
            ROS_ERROR_STREAM("Failed to create encoder element");
            exitOnMainThread(1);
          }
          g_object_set( G_OBJECT(_encode), "target", 1, NULL);
          g_object_set( G_OBJECT(_encode), "bitrate", _bitrate, NULL);

          gst_bin_add_many( GST_BIN(_pipeline), _source, _filter, _convert, _encode, _sink, NULL);

          link_ok = gst_element_link_many(_source, _filter, _convert, _encode, _sink, NULL);
        } else if (_format == "wave") {
          if (dst_type == "appsink") {
            g_object_set( G_OBJECT(_sink), "caps", caps, NULL);
            gst_caps_unref(caps);
            gst_bin_add_many( GST_BIN(_pipeline), _source, _sink, NULL);

            // //控制发布频率 *** 这个ChatGPT搜出来的方法不行
            // float pub_frequency = 1;
            // float blocksize_float = 1 / pub_frequency * _sample_rate * _channels * 2;
            // int blocksize = static_cast<int>(blocksize_float);
            // g_object_set(G_OBJECT(_sink), "blocksize", blocksize, NULL);
            // gint current_blocksize;
            // g_object_get(G_OBJECT(_sink), "blocksize", &current_blocksize, NULL);
            // ROS_INFO_STREAM("Blocksize: " << current_blocksize);

            link_ok = gst_element_link_many( _source, _sink, NULL);
          } else {
            _filter = gst_element_factory_make("wavenc", "filter");
            gst_bin_add_many( GST_BIN(_pipeline), _source, _filter, _sink, NULL);
            link_ok = gst_element_link_many( _source, _filter, _sink, NULL);
          }
        } else {
          ROS_ERROR_STREAM("format must be \"wave\" or \"mp3\"");
          exitOnMainThread(1);
        }
        /*}
        else
        {
          _sleep_time = 10000;
          _source = gst_element_factory_make("filesrc", "source");
          g_object_set(G_OBJECT(_source), "location", source_type.c_str(), NULL);

          gst_bin_add_many( GST_BIN(_pipeline), _source, _sink, NULL);
          gst_element_link_many(_source, _sink, NULL);
        }
        */

        if (!link_ok) {
          ROS_ERROR_STREAM("Unsupported media type.");
          exitOnMainThread(1);
        }
        
        gst_element_set_state(GST_ELEMENT(_pipeline), GST_STATE_PLAYING);

        _gst_thread = boost::thread( boost::bind(g_main_loop_run, _loop) );

        audio_common_msgs::AudioInfo info_msg;
        info_msg.channels = _channels;
        info_msg.sample_rate = _sample_rate;
        info_msg.sample_format = _sample_format;
        info_msg.bitrate = _bitrate;
        info_msg.coding_format = _format;
        _pub_info.publish(info_msg);
      }

      ~RosGstCapture()
      {
        g_main_loop_quit(_loop);
        gst_element_set_state(_pipeline, GST_STATE_NULL);
        gst_object_unref(_pipeline);
        g_main_loop_unref(_loop);
      }

      void exitOnMainThread(int code)
      {
        exit(code);
      }

      void publish( const audio_common_msgs::AudioData &msg )
      {
        _pub.publish(msg);
      }

      void publishStamped( const audio_common_msgs::AudioDataStamped &msg )
      {
        _pub_stamped.publish(msg);
      }

      // static GstFlowReturn onNewBuffer (GstAppSink *appsink, gpointer userData)
      // {
      //   RosGstCapture *server = reinterpret_cast<RosGstCapture*>(userData);
      //   GstMapInfo map;

      //   GstSample *sample;
      //   g_signal_emit_by_name(appsink, "pull-sample", &sample);

      //   GstBuffer *buffer = gst_sample_get_buffer(sample);

      //   gst_buffer_map(buffer, &map, GST_MAP_READ);

      //   _data_buffer.insert(_data_buffer.end(), map.data, map.data + map.size);
        
      //   // msg.data.resize( map.size );

      //   // memcpy( &msg.data[0], map.data, map.size );
      //   // stamped_msg.audio = msg;

      //   gst_buffer_unmap(buffer, &map);
      //   gst_sample_unref(sample);
        

      //   if (_data_buffer.size() == _data_buffer_length)
      //   {
      //     audio_common_msgs::AudioData msg;
      //     audio_common_msgs::AudioDataStamped stamped_msg;

      //     msg.data.resize(_data_buffer_length);
      //     memcpy(&msg.data[0], _data_buffer.data(), _data_buffer_length);
      //     stamped_msg.audio = msg;

      //     if( ros::Time::isSimTime() )
      //     {
      //       stamped_msg.header.stamp = ros::Time::now();
      //     }
      //     else
      //     {
      //       GstClockTime buffer_time = gst_element_get_base_time(server->_source)+GST_BUFFER_PTS(buffer);
      //       stamped_msg.header.stamp.fromNSec(buffer_time);
      //     }

      //     server->publish(msg);
      //     server->publishStamped(stamped_msg);
      //     _data_buffer.clear();
      //   }
      //   return GST_FLOW_OK;
      // }

      static GstFlowReturn onNewBuffer (GstAppSink *appsink, gpointer userData)
      {
        RosGstCapture *server = reinterpret_cast<RosGstCapture*>(userData);
        GstMapInfo map;

        GstSample *sample;
        g_signal_emit_by_name(appsink, "pull-sample", &sample);

        GstBuffer *buffer = gst_sample_get_buffer(sample);

        gst_buffer_map(buffer, &map, GST_MAP_READ);

        _data_buffer.insert(_data_buffer.end(), map.data, map.data + map.size);
        
        // msg.data.resize( map.size );

        // memcpy( &msg.data[0], map.data, map.size );
        // stamped_msg.audio = msg;

        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        

        if (_data_buffer.size() >= _data_buffer_length)
        {
          audio_common_msgs::AudioData msg;
          audio_common_msgs::AudioDataStamped stamped_msg;

          msg.data.resize(_data_buffer_length);
          memcpy(&msg.data[0], _data_buffer.data(), _data_buffer_length);
          stamped_msg.audio = msg;

          if( ros::Time::isSimTime() )
          {
            stamped_msg.header.stamp = ros::Time::now();
          }
          else
          {
            GstClockTime buffer_time = gst_element_get_base_time(server->_source)+GST_BUFFER_PTS(buffer);
            stamped_msg.header.stamp.fromNSec(buffer_time);
          }

          server->publish(msg);
          server->publishStamped(stamped_msg);
          _data_buffer.erase(_data_buffer.begin(), _data_buffer.begin() + _data_buffer_length);
        }
        return GST_FLOW_OK;
      }

      static gboolean onMessage (GstBus *bus, GstMessage *message, gpointer userData)
      {
        RosGstCapture *server = reinterpret_cast<RosGstCapture*>(userData);
        GError *err;
        gchar *debug;

        gst_message_parse_error(message, &err, &debug);
        ROS_ERROR_STREAM("gstreamer: " << err->message);
        g_error_free(err);
        g_free(debug);
        g_main_loop_quit(server->_loop);
        server->exitOnMainThread(1);
        return FALSE;
      }

    private:
      ros::NodeHandle _nh;
      ros::Publisher _pub;
      ros::Publisher _pub_stamped;
      ros::Publisher _pub_info;

      boost::thread _gst_thread;

      GstElement *_pipeline, *_source, *_filter, *_sink, *_convert, *_encode;
      GstBus *_bus;
      int _bitrate, _channels, _depth, _sample_rate, _pub_frequency;
      GMainLoop *_loop;
      std::string _format, _sample_format;

       
  };
}

int main (int argc, char **argv)
{
  ros::init(argc, argv, "audio_capture");
  gst_init(&argc, &argv);

  audio_transport::RosGstCapture server;
  ros::spin();
}
