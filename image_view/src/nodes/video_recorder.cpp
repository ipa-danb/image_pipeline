/****************************************************************************
* Software License Agreement (Apache License)
*
*     Copyright (C) 2012-2013 Open Source Robotics Foundation
*
*     Licensed under the Apache License, Version 2.0 (the "License");
*     you may not use this file except in compliance with the License.
*     You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
*     Unless required by applicable law or agreed to in writing, software
*     distributed under the License is distributed on an "AS IS" BASIS,
*     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*     See the License for the specific language governing permissions and
*     limitations under the License.
*
*****************************************************************************/

#include <opencv2/highgui/highgui.hpp>
#include <ros/ros.h>
#include <sensor_msgs/image_encodings.h>
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <camera_calibration_parsers/parse.h>
#include "std_srvs/Trigger.h"
#include <image_view/String.h>
#include <atomic>
#if CV_MAJOR_VERSION == 3
#include <opencv2/videoio.hpp>
#endif

cv::VideoWriter outputVideo;

int g_count = 0;
ros::Time g_last_wrote_time = ros::Time(0);
std::string encoding;
std::string codec;
int fps;
std::string filename;
std::string filename_orig;
double min_depth_range;
double max_depth_range;
bool use_dynamic_range;
int colormap;
bool save_all_video;

std::atomic<bool> recording(true);

void callback(const sensor_msgs::ImageConstPtr& image_msg)
{
    if (recording)
    {
        if (!outputVideo.isOpened()) {

            cv::Size size(image_msg->width, image_msg->height);

            outputVideo.open(filename,
#if CV_MAJOR_VERSION == 3
                    cv::VideoWriter::fourcc(codec.c_str()[0],
#else
                    CV_FOURCC(codec.c_str()[0],
#endif
                              codec.c_str()[1],
                              codec.c_str()[2],
                              codec.c_str()[3]),
                    fps,
                    size,
                    true);

            if (!outputVideo.isOpened())
            {
                ROS_ERROR("Could not create the output video! Check filename and/or support for codec.");
                exit(-1);
            }

            ROS_INFO_STREAM("Starting to record " << codec << " video at " << size << "@" << fps << "fps. Press Ctrl+C to stop recording." );

        }

        if ((image_msg->header.stamp - g_last_wrote_time) < ros::Duration(1.0 / fps))
        {
          // Skip to get video with correct fps
          return;
        }

        try
        {
          cv_bridge::CvtColorForDisplayOptions options;
          options.do_dynamic_scaling = use_dynamic_range;
          options.min_image_value = min_depth_range;
          options.max_image_value = max_depth_range;
          options.colormap = colormap;
          const cv::Mat image = cv_bridge::cvtColorForDisplay(cv_bridge::toCvShare(image_msg), encoding, options)->image;
          if (!image.empty()) {
            outputVideo << image;
            ROS_INFO_STREAM("Recording frame " << g_count << "\x1b[1F");
            g_count++;
            g_last_wrote_time = image_msg->header.stamp;
          } else {
              ROS_WARN("Frame skipped, no data!");
          }
        } catch(cv_bridge::Exception)
        {
            ROS_ERROR("Unable to convert %s image to %s", image_msg->encoding.c_str(), encoding.c_str());
            return;
        }
    }
}

std::string stamp_filename(const std::string& filename)
{
    std::size_t found = filename.find_last_of("/\\");
    std::string path = filename.substr(0, found + 1);
    std::string basename = filename.substr(found + 1);
    std::stringstream ss;
    ss <<  basename << "_" << ros::Time::now().toNSec() << ".avi";
    std::string filename_stamped = path + ss.str();
    ROS_INFO("Video recording to %s", filename_stamped.c_str());
    return filename_stamped;
}

bool start_cb(std_srvs::Trigger::Request  &req,
         std_srvs::Trigger::Response &res)
{
    filename = stamp_filename(filename_orig);
    ROS_INFO("STARTING");
    recording = true;
    return true;
}

bool start_string_cb(image_view::String::Request  &req,
         image_view::String::Response &res)
{
    filename = stamp_filename(std::string(req.str).c_str());
    ROS_INFO("STARTING with name %s",filename.c_str());
    recording = true;
    return true;
}

bool stop_cb(std_srvs::Trigger::Request  &req,
         std_srvs::Trigger::Response &res)
{
    ROS_INFO("Stoping");
    recording = false;
    outputVideo.release();
    return true;
}

int main(int argc, char** argv)
{
    ros::init(argc, argv, "video_recorder", ros::init_options::AnonymousName);
    ros::NodeHandle nh;
    ros::NodeHandle local_nh("~");
    local_nh.param("filename", filename_orig, std::string("output"));
    bool stamped_filename;
    local_nh.param("stamped_filename", stamped_filename, false);
    local_nh.param("fps", fps, 15);
    local_nh.param("codec", codec, std::string("MJPG"));
    local_nh.param("encoding", encoding, std::string("bgr8"));
    // cv_bridge::CvtColorForDisplayOptions
    local_nh.param("min_depth_range", min_depth_range, 0.0);
    local_nh.param("max_depth_range", max_depth_range, 0.0);
    local_nh.param("use_dynamic_depth_range", use_dynamic_range, false);
    local_nh.param("colormap", colormap, -1);
    local_nh.param("save_all_video", save_all_video, false);

    if (stamped_filename) {
        filename = stamp_filename(filename_orig);
    }
    else
    {
        filename = filename_orig;
    }

    ros::ServiceServer srv_start;
    ros::ServiceServer srv_start_named;
    ros::ServiceServer srv_stop;

    if (save_all_video)
    {
        srv_start = local_nh.advertiseService("start", start_cb);
        srv_start_named = local_nh.advertiseService("start_named", start_string_cb);
        srv_stop = local_nh.advertiseService("stop", stop_cb);
        recording = false;
    }

    if (codec.size() != 4) {
        ROS_ERROR("The video codec must be a FOURCC identifier (4 chars)");
        exit(-1);
    }

    image_transport::ImageTransport it(nh);
    std::string topic = nh.resolveName("image");
    image_transport::Subscriber sub_image = it.subscribe(topic, 1, callback);

    ROS_INFO_STREAM("Waiting for topic " << topic << "...");
    ros::spin();
    if (recording){
        std::cout << "\nVideo saved as " << filename << std::endl;
    }
}
