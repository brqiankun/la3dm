#include <string>
#include <iostream>
#include <ros/ros.h>
#include <pcl_ros/transforms.h>
#include <pcl/filters/voxel_grid.h>
#include "markerarray_pub.h"
#include "gpoctomap.h"
#include <dbg.h>

tf::TransformListener *listener;
std::string frame_id("camera_init");
std::string child_frame_id("/aft_mapped");
la3dm::GPOctoMap *map;

la3dm::MarkerArrayPub *m_pub_occ, *m_pub_free;

tf::Vector3 last_position;
tf::Quaternion last_orientation;
bool first = true;
double position_change_thresh = 0.1;
double orientation_change_thresh = 0.2;
bool updated = false;

//Universal parameters
std::string map_topic_occ("/occupied_cells_vis_array");
std::string map_topic_free("/free_cells_vis_array");
double max_range = -1;
double resolution = 0.1;
int block_depth = 4;
double sf2 = 1.0;
double ell = 1.0;
double free_resolution = 0.1;
double ds_resolution = 0.1;
double free_thresh = 0.3;
double occupied_thresh = 0.7;
double min_z = 0;
double max_z = 0;
bool original_size = true;

//parameters for GPOctomap
double noise = 0.01;
double l = 100;
double min_var = 0.001;
double max_var = 1000;
double max_known_var = 0.02;

void cloudHandler(const sensor_msgs::PointCloud2ConstPtr &cloud) {
    
    tf::StampedTransform transform;
    try {
        listener->waitForTransform(frame_id, child_frame_id, cloud->header.stamp, ros::Duration(2.0));
        listener->lookupTransform(frame_id, child_frame_id, cloud->header.stamp, transform); //ros::Time::now() -- Don't use this because processing time delay breaks it
    } catch (tf::TransformException ex) {
        ROS_ERROR("%s", ex.what());
        return;
    }

    ros::Time start = ros::Time::now();
    la3dm::point3f origin;
    tf::Vector3 translation = transform.getOrigin();
    tf::Quaternion orientation = transform.getRotation();

    if (first || orientation.angleShortestPath(last_orientation) > orientation_change_thresh || 
        translation.distance(last_position) > position_change_thresh) {
        ROS_INFO_STREAM("Cloud received");
        
        last_position = translation;
        last_orientation = orientation;
        origin.x() = (float) translation.x();
        origin.y() = (float) translation.y();
        origin.z() = (float) translation.z();
        std::printf("origin.x: %f, origin.y: %f, origin.z: %f\n", origin.x(), origin.y(), origin.z());

        // sensor_msgs::PointCloud2 cloud_map;
        //                           target_frame, cloud_in, cloud_out, tf_listener transform a pointcloud in a given target TF frame using a TransformListener.
        // pcl_ros::transformPointCloud(frame_id, *cloud, cloud_map, *listener);    // ??? 里程计发送的点云已经完成位姿变换

        la3dm::PCLPointCloud::Ptr pcl_cloud (new la3dm::PCLPointCloud());
        pcl::fromROSMsg(*cloud, *pcl_cloud);

        //downsample for faster mapping
        la3dm::PCLPointCloud filtered_cloud;
        pcl::VoxelGrid<pcl::PointXYZ> filterer;
        filterer.setInputCloud(pcl_cloud);
        filterer.setLeafSize(ds_resolution, ds_resolution, ds_resolution);
        filterer.filter(filtered_cloud);

        dbg(filtered_cloud.size());
        if(filtered_cloud.size() > 5){
            map->insert_pointcloud(filtered_cloud, origin, (float) resolution, (float) free_resolution, (float) max_range);
        }

        ros::Time end = ros::Time::now();
        ROS_INFO_STREAM("One cloud finished in " << (end - start).toSec() << "s");
        updated = true;
    } else {
        dbg(orientation.angleShortestPath(last_orientation));
        dbg(translation.distance(last_position));
        dbg(first);
    }


    if (updated) {
        ros::Time start2 = ros::Time::now();

        m_pub_occ->clear();
        m_pub_free->clear();

        for (auto it = map->begin_leaf(); it != map->end_leaf(); ++it) {

            la3dm::point3f p = it.get_loc();

            if (it.get_node().get_state() == la3dm::State::OCCUPIED) {
                if (original_size) {
                    m_pub_occ->insert_point3d(p.x(), p.y(), p.z(), min_z, max_z, it.get_size());
                } else {
                    auto pruned = it.get_pruned_locs();
                    for (auto n = pruned.cbegin(); n < pruned.cend(); ++n) {
                        m_pub_occ->insert_point3d(n->x(), n->y(), n->z(), min_z, max_z, map->get_resolution());
                    }
                }
            } else if (it.get_node().get_state() == la3dm::State::FREE) {
                if (original_size) {
                    m_pub_free->insert_point3d(p.x(), p.y(), p.z(), min_z, max_z, it.get_size(), it.get_node().get_prob());
                } else {
                    auto pruned = it.get_pruned_locs();
                    for (auto n = pruned.cbegin(); n < pruned.cend(); ++n) {
                        m_pub_free->insert_point3d(n->x(), n->y(), n->z(), min_z, max_z, map->get_resolution(), it.get_node().get_prob());
                    }
                }
            }
        }
        updated = false;

        m_pub_occ->publish();
        m_pub_free->publish();

        ros::Time end2 = ros::Time::now();
        ROS_INFO_STREAM("One map published in " << (end2 - start2).toNSec() << "nano second");
    }
    first = false;
}

int main(int argc, char **argv) {
    ros::init(argc, argv, "gpoctomap_server");
    ros::NodeHandle nh("~");
    //incoming pointcloud topic, this could be put into the .yaml too
    std::string cloud_topic("/velodyne_cloud_registered");

    //Universal parameters
    nh.param<std::string>("topic", map_topic_occ, map_topic_occ);
    nh.param<std::string>("topic_free", map_topic_free, map_topic_free);
    nh.param<double>("max_range", max_range, max_range);
    nh.param<double>("resolution", resolution, resolution);
    nh.param<int>("block_depth", block_depth, block_depth);
    nh.param<double>("sf2", sf2, sf2);
    nh.param<double>("ell", ell, ell);
    nh.param<double>("free_resolution", free_resolution, free_resolution);
    nh.param<double>("ds_resolution", ds_resolution, ds_resolution);
    nh.param<double>("free_thresh", free_thresh, free_thresh);
    nh.param<double>("occupied_thresh", occupied_thresh, occupied_thresh);
    nh.param<double>("min_z", min_z, min_z);
    nh.param<double>("max_z", max_z, max_z);
    nh.param<bool>("original_size", original_size, original_size);

    //parameters for GPOctomap
    nh.param<double>("noise", noise, noise);
    nh.param<double>("l", l, l);
    nh.param<double>("min_var", min_var, min_var);
    nh.param<double>("max_var", max_var, max_var);
    nh.param<double>("max_known_var", max_known_var, max_known_var);

    ROS_INFO_STREAM("Parameters:" << std::endl <<
            "topic: " << map_topic_occ << std::endl <<
            "max_range: " << max_range << std::endl <<
            "resolution: " << resolution << std::endl <<
            "block_depth: " << block_depth << std::endl <<
            "sf2: " << sf2 << std::endl <<
            "ell: " << ell << std::endl <<
            "l: " << l << std::endl <<
            "min_var: " << min_var << std::endl <<
            "max_var: " << max_var << std::endl <<
            "max_known_var: " << max_known_var << std::endl <<
            "free_resolution: " << free_resolution << std::endl <<
            "ds_resolution: " << ds_resolution << std::endl <<
            "free_thresh: " << free_thresh << std::endl <<
            "occupied_thresh: " << occupied_thresh << std::endl <<
            "min_z: " << min_z << std::endl <<
            "max_z: " << max_z << std::endl <<
            "original_size: " << original_size
            );

    map = new la3dm::GPOctoMap(resolution, block_depth, sf2, ell, noise, l, min_var, 
                               max_var, max_known_var, free_thresh, occupied_thresh);
    
    ros::Subscriber point_sub = nh.subscribe<sensor_msgs::PointCloud2>(cloud_topic, 1, cloudHandler);
    m_pub_occ = new la3dm::MarkerArrayPub(nh, map_topic_occ, resolution);
    m_pub_free = new la3dm::MarkerArrayPub(nh, map_topic_free, resolution);

    listener = new tf::TransformListener();
    
    while(ros::ok()) {
    	ros::spin();
    }

    return 0;
}
