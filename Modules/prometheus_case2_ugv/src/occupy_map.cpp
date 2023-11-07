#include <occupy_map.h>

namespace prometheus_case2_ugv
{
// 初始化函数
void Occupy_map::init(ros::NodeHandle& nh)
{
    // 集群数量
    nh.param("case2_ugv/swarm_num_ugv", swarm_num_ugv, 1);
    // 无人机编号 1号无人机则为1
    nh.param("case2_ugv/ugv_id", ugv_id, 0);
    nh.param("case2_ugv/ugv_height", ugv_height, 0.1);
    nh.param("case2_ugv/sim_mode", sim_mode, true);
    nh.param("case2_ugv/odom_inflate", odom_inflate_, 0.6);
    nh.param("case2_ugv/cost_inflate", cost_inflate, 5);
    // 地图原点
    nh.param("map/origin_x", origin_(0), -5.0);
    nh.param("map/origin_y", origin_(1), -5.0);
    nh.param("map/origin_z", origin_(2), -0.5);
    // 地图实际尺寸，单位：米
    nh.param("map/map_size_x", map_size_3d_(0), 10.0);
    nh.param("map/map_size_y", map_size_3d_(1), 10.0);
    nh.param("map/map_size_z", map_size_3d_(2), 2.0);
    // localmap slide window
    nh.param("map/queue_size", queue_size, -1);
    // show border
    nh.param("map/border", show_border, false);
    // 地图分辨率，单位：米
    nh.param("map/resolution", resolution_,  0.2);
    // 地图膨胀距离，单位：米
    nh.param("map/inflate", inflate_,  0.3);

    ugv_name = "/ugv" + std::to_string(ugv_id);
    // 发布 地图rviz显示
    global_pcl_pub = nh.advertise<sensor_msgs::PointCloud2>(ugv_name + "/prometheus/planning/global_pcl",  1); 
    // 发布膨胀后的点云
    inflate_pcl_pub = nh.advertise<sensor_msgs::PointCloud2>(ugv_name + "/prometheus/planning/global_inflate_pcl", 1);

    // 主循环执行定时器
    pcl_pub = nh.createTimer(ros::Duration(0.2), &Occupy_map::pub_pcl_cb, this);   

    // 全局地图点云指针
    global_point_cloud_map.reset(new pcl::PointCloud<pcl::PointXYZ>);
    global_ugv_pcl.reset(new pcl::PointCloud<pcl::PointXYZ>);
    // 膨胀点云指针
    cloud_inflate_vis_.reset(new pcl::PointCloud<pcl::PointXYZ>);
    // 传入点云指针（临时指针）
    input_point_cloud.reset(new pcl::PointCloud<pcl::PointXYZ>);
    // tf变换后点云指针（临时指针）
    transformed_cloud.reset(new pcl::PointCloud<pcl::PointXYZ>);
    // 过滤后点云指针（临时指针）
    pcl_ptr.reset(new pcl::PointCloud<pcl::PointXYZ>);
    // 局部地图滑窗指示器
    st_it = 0;
    // 存储的上一帧odom
    f_x = f_y = f_z = f_pitch = f_yaw = f_roll = 0.0;

    this->inv_resolution_ = 1.0 / resolution_;
    for (int i = 0; i < 3; ++i)
    {
        // 占据图尺寸 = 地图尺寸 / 分辨率
        grid_size_(i) = ceil(map_size_3d_(i) / resolution_);
    }
    
    // 占据容器的大小 = 占据图尺寸 x*y*z
    occupancy_buffer_.resize(grid_size_(0) * grid_size_(1) * grid_size_(2));
    cost_map_.resize(grid_size_(0) * grid_size_(1) * grid_size_(2));
    fill(occupancy_buffer_.begin(), occupancy_buffer_.end(), 0.0);
    fill(cost_map_.begin(), cost_map_.end(), 0.0);

    min_range_ = origin_;
    max_range_ = origin_ + map_size_3d_;   

    min_range_(2) = ugv_height - 2*resolution_;
    max_range_(2) = ugv_height + 2*resolution_;
    get_gpcl = false;
    get_lpcl = false;
    get_laser = false;
    // 生成地图边界：点云形式
	double dist = 0.1; //每多少距离一个点
	int numdist_x = (max_range_(0)-min_range_(0))/dist; //x的点数
	int numdist_y = (max_range_(1)-min_range_(1))/dist; //y的点数 
	int numdist = 2*(numdist_x+numdist_y); //总点数
	border.width = numdist;
    border.height = 1;
    border.points.resize(numdist);

    inflate_index_ugv = 0;
    ifn = ceil(odom_inflate_ * inv_resolution_);
    for(int x = -ifn; x <= ifn; x++)
        for(int y = -ifn; y <= ifn;)
        {
            enum_p_ugv[inflate_index_ugv++] << x*resolution_, y*resolution_, 0.0;
            if(x == -ifn || x == ifn) y++;
            else y += 2*ifn;
        }

    for(int x = -ifn-1; x <= ifn+1; x++)
        for(int y = -ifn-1; y <= ifn+1;)
        {
            enum_p_ugv[inflate_index_ugv++] << x*resolution_, y*resolution_, 0.0;
            if(x == -ifn-1 || x == ifn+1) y++;
            else y += 2*ifn+2;
        }

    // 膨胀格子数 = 膨胀距离/分辨率
    // ceil返回大于或者等于指定表达式的最小整数
    ifn = ceil(inflate_ * inv_resolution_);
    
    inflate_index = 0;
    for(int x = -ifn; x <= ifn; x++)
        for(int y = -ifn; y <= ifn;)
        {
            enum_p[inflate_index++] << x*resolution_, y*resolution_, 0.0;
            if(x == -ifn || x == ifn) y++;
            else y += 2*ifn;
        }

    cost_index = 0;
    // for(int x = -cost_inflate; x <= cost_inflate; x++)
    //     for(int y = -cost_inflate; y <= cost_inflate; y++)
    //     {
    //         int tmp_dis = x*x + y*y;
    //         if(tmp_dis <= cost_inflate*cost_inflate)
    //         {
    //             enum_p_cost[cost_index++] << x*resolution_, y*resolution_, tmp_dis;
    //         }
            
    //     }
    for(int x = -ifn-cost_inflate; x <= ifn+cost_inflate; x++)
        for(int y = -ifn-cost_inflate; y <= ifn+cost_inflate;)
        {
            int tmp_dis = x*x + y*y;
            if(tmp_dis <= (ifn+cost_inflate)*(ifn+cost_inflate))
            {
                enum_p_cost[cost_index++] << x*resolution_, y*resolution_, tmp_dis;
            }
            if(x == -ifn-cost_inflate || x == ifn+cost_inflate) y++;
            else y += 2*ifn+2*cost_inflate;
        }
    printf("cost map %d %d\n",cost_inflate,cost_index);

    for(int i=0 ; i<numdist_x; i++) //x边界
    {
        border.points[i].x = min_range_(0)+i*dist;
        border.points[i].y = min_range_(1);
        border.points[i].z = min_range_(2);

        border.points[i+numdist_x].x = min_range_(0)+i*dist;
        border.points[i+numdist_x].y = max_range_(1);
        border.points[i+numdist_x].z = min_range_(2);
	}
	
    for(int i=0 ; i<numdist_y; i++) //y边界
    {
        border.points[i+2*numdist_x].x = min_range_(0);
        border.points[i+2*numdist_x].y = min_range_(1)+i*dist;
        border.points[i+2*numdist_x].z = min_range_(2);

        border.points[i+2*numdist_x+numdist_y].x = max_range_(0);
        border.points[i+2*numdist_x+numdist_y].y = min_range_(1)+i*dist;
        border.points[i+2*numdist_x+numdist_y].z = min_range_(2);
    }
}

// 地图更新函数 - 输入：全局点云
void Occupy_map::map_update_gpcl(const sensor_msgs::PointCloud2ConstPtr & global_point)
{
    // 全局地图只更新一次
    if(get_gpcl)
    {
        return;
    }

    get_gpcl = true;
    has_global_point = true;
    pcl::fromROSMsg(*global_point,*input_point_cloud);
    global_point_cloud_map = input_point_cloud;
    inflate_point_cloud(); 
}

// 地图更新函数 - 输入：局部点云
void Occupy_map::map_update_lpcl(const sensor_msgs::PointCloud2ConstPtr & local_point, const nav_msgs::Odometry & odom)
{
    // 由sensor_msgs::PointCloud2 转为 pcl::PointCloud<pcl::PointXYZ>
    pcl::fromROSMsg(*local_point,*input_point_cloud);
    
    has_global_point = true;
    
    if(sim_mode)
    {
        if(queue_size <= 0) // without slide windows
        {
            // map_generator生成的点云为world坐标系
            *global_point_cloud_map += *input_point_cloud;
        }
        else // with slide windows
        {
            // slide windows with size: $queue_size
            point_cloud_pair[st_it] = *input_point_cloud; // 加入新点云到滑窗
            st_it = (st_it + 1) % queue_size; // 指向下一个移除的点云位置

            // 累计局部地图：需要20个加法，O（1）内存；增量式：需要19个加法，O（1.5）内存
            global_point_cloud_map.reset(new pcl::PointCloud<pcl::PointXYZ>);
            map<int,pcl::PointCloud<pcl::PointXYZ>>::iterator iter;
            for(iter = point_cloud_pair.begin(); iter != point_cloud_pair.end(); iter++)
            {
                *global_point_cloud_map += iter->second;
            }
        }

        // downsample
        *pcl_ptr = *global_point_cloud_map;
        vg.setInputCloud(pcl_ptr);
        vg.setLeafSize(0.05f, 0.05f, 0.05f); // 下采样叶子节点大小（3D容器）
        vg.filter(*global_point_cloud_map);
        inflate_point_cloud(); 
    }else
    {
        local_map_merge_odom(odom);
    }
}

// 地图更新函数 - 输入：laser
void Occupy_map::map_update_laser(const sensor_msgs::LaserScanConstPtr & local_point, const nav_msgs::Odometry & odom)
{
    // 参考网页:http://wiki.ros.org/laser_geometry
    // sensor_msgs::LaserScan 转为 sensor_msgs::PointCloud2 格式
    projector_.projectLaser(*local_point, input_laser_scan);
    // 再由sensor_msgs::PointCloud2 转为 pcl::PointCloud<pcl::PointXYZ>
    pcl::fromROSMsg(input_laser_scan,*input_point_cloud);

    if(sim_mode)
    {
        // to do
    }else
    {
        local_map_merge_odom(odom);
    }
}

// 工具函数：合并局部地图 - 输入：odom以及局部点云
void Occupy_map::local_map_merge_odom(const nav_msgs::Odometry & odom)
{
    // 从odom中取得6DOF
    double x, y, z, roll, pitch, yaw;
    // 平移（xyz）
    x = odom.pose.pose.position.x;
    y = odom.pose.pose.position.y;
    z = odom.pose.pose.position.z;
    // 旋转（从四元数到欧拉角）
    tf::Quaternion orientation;
    tf::quaternionMsgToTF(odom.pose.pose.orientation, orientation);    
    tf::Matrix3x3(orientation).getRPY(roll, pitch, yaw);

    // ugv is moving
    bool pos_change = (abs(x-f_x)>0.1) || (abs(y-f_y)>0.1);
    // update map even though ugv doesn't move
    static int update_num=0;
    update_num++;

    // merge local points to local map
    if(pos_change || global_point_cloud_map == nullptr || update_num>1) 
    {
        update_num = 0;
        
        // accumulate pointcloud according to odom
        pcl::transformPointCloud(*input_point_cloud,*transformed_cloud,pcl::getTransformation(x, y, z, 0.0, 0.0, yaw));
        if(queue_size <= 0) // without slide windows
        {
            *transformed_cloud += *global_point_cloud_map;
        }
        else // with slide windows
        {
            // slide windows with size: $queue_size
            point_cloud_pair[st_it] = *transformed_cloud; // 加入新点云到滑窗
            st_it = (st_it + 1) % queue_size; // 指向下一个移除的点云位置

            // 累计局部地图：需要20个加法，O（1）内存；增量式：需要19个加法，O（1.5）内存
            transformed_cloud.reset(new pcl::PointCloud<pcl::PointXYZ>);
            map<int,pcl::PointCloud<pcl::PointXYZ>>::iterator iter;
            for(iter = point_cloud_pair.begin(); iter != point_cloud_pair.end(); iter++)
            {
                *transformed_cloud += iter->second;
            }
        }

        // remove outlier
        // sor.setInputCloud(transformed_cloud);
        // sor.setMeanK(20); // kNN neighbor
        // sor.setStddevMulThresh(1.0); // threshold
        // sor.setNegative(false);
        // sor.filter(*transformed_cloud);

        // downsample
        vg.setInputCloud(transformed_cloud);
        vg.setLeafSize(0.2f, 0.2f, 0.2f); // 下采样叶子节点大小（3D容器）
        vg.filter(*global_point_cloud_map);

        // store last odom data
        f_x = x;
        f_y = y;
        f_z = z;
        f_roll = roll;
        f_pitch = pitch;
        f_yaw = yaw;
        // global map flag
        has_global_point = true;

        inflate_point_cloud(); 
    }
    else
    {
        has_global_point = false;
    } 
}

// function: update global ugv occupy grid (10Hz, defined by fsm)
void Occupy_map::ugv_pcl_update(Eigen::Vector3d *input_ugv_odom, bool *get_ugv_odom)
{

    // cout << BLUE << "11111 " << TAIL <<endl;
    Eigen::Vector3d p3d_inf;

    // reset ugv occupy grid with last ugv pcl (same as normal occupy map)
    // for(int i = 0; i < global_ugv_pcl->points.size(); i++)
    // {
    //     p3d_inf(0) = global_ugv_pcl->points[i].x;
    //     p3d_inf(1) = global_ugv_pcl->points[i].y;
    //     p3d_inf(2) = global_ugv_pcl->points[i].z;
    //     this->setOccupancy(p3d_inf, 0); // reset to 0
    // }

    // update global ugv occupy grid with input ugv odom
    pcl::PointXYZ pt;
    global_ugv_pcl.reset(new pcl::PointCloud<pcl::PointXYZ>);

    for(int i = 1; i <= swarm_num_ugv; i++)
    {
        if(i == ugv_id)
        {
            continue;
        }
        
        if(get_ugv_odom[i])
            for(int j = 0; j < inflate_index_ugv; j++)
            {
                pt.x = input_ugv_odom[i][0] + enum_p_ugv[j](0);
                pt.y = input_ugv_odom[i][1] + enum_p_ugv[j](1);
                pt.z = input_ugv_odom[i][2] + enum_p_ugv[j](2);

                global_ugv_pcl->points.push_back(pt);
            }
    }
    global_ugv_pcl->width = global_ugv_pcl->points.size();
    global_ugv_pcl->height = 1;
    global_ugv_pcl->is_dense = true;
}

// 当global_planning节点接收到点云消息更新时，进行设置点云指针并膨胀
// Astar规划路径时，采用的是此处膨胀后的点云（setOccupancy只在本函数中使用）
void Occupy_map::inflate_point_cloud(void)
{

    // cout << BLUE << "22222 " << TAIL <<endl;

    if(!has_global_point)
    {
        return;
    }
    
    if(get_gpcl)
    {
        // occupancy_buffer_清零，不需要清0 
        fill(occupancy_buffer_.begin(), occupancy_buffer_.end(), 0.0);
        fill(cost_map_.begin(), cost_map_.end(), 0.0);
    }else if(queue_size > 0)
    {
        // queue_size设置为大于0时，代表仅使用过去一定数量的点云
        fill(occupancy_buffer_.begin(), occupancy_buffer_.end(), 0.0);
        fill(cost_map_.begin(), cost_map_.end(), 0.0);
    }

    //记录开始时间
    ros::Time time_start = ros::Time::now();

    // 转化为PCL的格式进行处理
    pcl::PointCloud<pcl::PointXYZ> latest_global_cloud_ = *global_point_cloud_map;

    if ((int)latest_global_cloud_.points.size() == 0)  
    {return;}
    
    cloud_inflate_vis_->clear();

    pcl::PointXYZ pt_inf;
    Eigen::Vector3d p3d, p3d_inf, p3d_cost;

    for(int i = 0; i < global_ugv_pcl->points.size(); i++)
    {
        p3d_inf(0) = global_ugv_pcl->points[i].x;
        p3d_inf(1) = global_ugv_pcl->points[i].y;
        p3d_inf(2) = global_ugv_pcl->points[i].z;
        this->setOccupancy(p3d_inf, 1); // set to 1
    }
    
    // 遍历全局点云中的所有点
    for (size_t i = 0; i < latest_global_cloud_.points.size(); ++i) 
    {
        // 取出第i个点
        p3d(0) = latest_global_cloud_.points[i].x;
        p3d(1) = latest_global_cloud_.points[i].y;
        p3d(2) = latest_global_cloud_.points[i].z;
        
        // 若取出的点不在地图内（膨胀时只考虑地图范围内的点）
        if(!isInMap(p3d))
        {
            continue;
        }

        // cost map update
        for(int j = 0; j < cost_index; j++)
        {
            p3d_cost(0) = p3d(0) + enum_p_cost[j](0);
            p3d_cost(1) = p3d(1) + enum_p_cost[j](1);
            p3d_cost(2) = p3d(2);
            this->updateCostMap(p3d_cost,1.0/enum_p_cost[j](2));
        }

        // 根据膨胀距离，膨胀该点
        for(int i = 0; i < inflate_index; i++)
        {
            p3d_inf(0) = p3d(0) + enum_p[i](0);
            p3d_inf(1) = p3d(1) + enum_p[i](1);
            p3d_inf(2) = p3d(2) + enum_p[i](2);

            // 若膨胀的点不在地图内（膨胀时只考虑地图范围内的点）
            if(!isInMap(p3d_inf))
            {
                continue;
            }

            pt_inf.x = p3d_inf(0);
            pt_inf.y = p3d_inf(1);
            pt_inf.z = p3d_inf(2);
            cloud_inflate_vis_->push_back(pt_inf);
            // 设置膨胀后的点被占据（不管他之前是否被占据）
            this->setOccupancy(p3d_inf, 1);

            // cost map update
            // for(int j = 0; j < cost_index; j++)
            // {
            //     p3d_cost(0) = p3d_inf(0) + enum_p_cost[j](0);
            //     p3d_cost(1) = p3d_inf(1) + enum_p_cost[j](1);
            //     p3d_cost(2) = p3d_inf(2);
            //     this->updateCostMap(p3d_cost,1.0/enum_p_cost[j](2));
            // }
        }
    }

    *cloud_inflate_vis_ += *global_ugv_pcl;
    // 加上border,仅用作显示作用
    if(show_border)
    {
        *cloud_inflate_vis_ += border;
    }

    static int exec_num=0;
    exec_num++;

    // 此处改为根据循环时间计算的数值
    if(exec_num == 50)
    {
        // 膨胀地图效率与地图大小有关
        cout << YELLOW << "Occupy map: inflate global point take " << (ros::Time::now()-time_start).toSec() <<" [s]. " << TAIL <<endl;
        exec_num=0;
    }else if(get_gpcl)
    {
        // cout << YELLOW << "Occupy map: inflate global point take " << (ros::Time::now()-time_start).toSec() <<" [s]. " << TAIL <<endl;
    } 
}

void Occupy_map::pub_pcl_cb(const ros::TimerEvent& e)
{
    // 发布未膨胀点云
    sensor_msgs::PointCloud2 global_env_;
    // 假设此时收到的就是全局pcl
    pcl::toROSMsg(*global_point_cloud_map,global_env_);
    global_env_.header.frame_id = "world";
    global_pcl_pub.publish(global_env_);

    // 发布膨胀点云
    sensor_msgs::PointCloud2 map_inflate_vis;
    pcl::toROSMsg(*cloud_inflate_vis_, map_inflate_vis);
    map_inflate_vis.header.frame_id = "world";
    inflate_pcl_pub.publish(map_inflate_vis);

}

void Occupy_map::setOccupancy(Eigen::Vector3d &pos, int occ) 
{
    if (occ != 1 && occ != 0) 
    {
        // cout << RED << "Occupy map: occ value error " << TAIL <<endl;
        return;
    }

    if (!isInMap(pos))
    {
        return;
    }

    Eigen::Vector3i id;
    posToIndex(pos, id);

    // 设置为占据/不占据 索引是如何索引的？ [三维地图 变 二维数组]
    // 假设10*10*10米，分辨率1米，buffer大小为 1000 （即每一个占格都对应一个buffer索引）
    // [0.1,0.1,0.1] 对应索引为[0,0,0]， buffer索引为 0  
    // [9.9,9.9,9.9] 对应索引为[9,9,9]， buffer索引为 900+90+9 = 999
    occupancy_buffer_[id(0) * grid_size_(1) * grid_size_(2) + id(1) * grid_size_(2) + id(2)] = occ;
}

void Occupy_map::updateCostMap(Eigen::Vector3d &pos, double cost)
{
    if(!isInMap(pos))
    {
        return;
    }

    Eigen::Vector3i id;
    posToIndex(pos, id);

    if(cost_map_[id(0) * grid_size_(1) * grid_size_(2) + id(1) * grid_size_(2) + id(2)] >= cost) return;
    cost_map_[id(0) * grid_size_(1) * grid_size_(2) + id(1) * grid_size_(2) + id(2)] = cost;
}

bool Occupy_map::isInMap(Eigen::Vector3d pos) 
{
    // min_range就是原点，max_range就是原点+地图尺寸
    // 比如设置0,0,0为原点，[0,0,0]点会被判断为不在地图里
    //　同时　对于２Ｄ情况，超出飞行高度的数据也会认为不在地图内部
    if (pos(0) < min_range_(0) + 1e-4 || pos(1) < min_range_(1) + 1e-4 || pos(2) < min_range_(2) + 1e-4) 
    {
        return false;
    }

    if (pos(0) > max_range_(0) - 1e-4 || pos(1) > max_range_(1) - 1e-4 || pos(2) > max_range_(2) - 1e-4) 
    {
        return false;
    }

    return true;
}

bool Occupy_map::check_safety(Eigen::Vector3d& pos, double check_distance)
{
    if(!isInMap(pos))
    {
        // 当前位置点不在地图内
        // cout << RED << "Occupy map, the odom point is not in map"  << TAIL <<endl;
        return 0;
    }
    Eigen::Vector3i id;
    posToIndex(pos, id);
    Eigen::Vector3i id_occ;
    Eigen::Vector3d pos_occ;

    int check_dist_xy = int(check_distance/resolution_);
    int check_dist_z=0;
    int cnt=0;
    for(int ix=-check_dist_xy; ix<=check_dist_xy; ix++){
        for(int iy=-check_dist_xy; iy<=check_dist_xy; iy++){
            for(int iz=-check_dist_z; iz<=check_dist_z; iz++){
                id_occ(0) = id(0)+ix;
                id_occ(1) = id(1)+iy;
                id_occ(2) = id(2)+iz;
                indexToPos(id_occ, pos_occ);
                if(!isInMap(pos_occ)){
                    return 0;
                }
                if(getOccupancy(id_occ)){
                    cnt++;             
                }
            }
        }
    }
    if(cnt>5){
        return 0;
    }
    return 1;

}

void Occupy_map::posToIndex(Eigen::Vector3d &pos, Eigen::Vector3i &id) 
{
    for (int i = 0; i < 3; ++i)
    {
        id(i) = floor((pos(i) - origin_(i)) * inv_resolution_);
    }
       
}

void Occupy_map::indexToPos(Eigen::Vector3i &id, Eigen::Vector3d &pos) 
{
    for (int i = 0; i < 3; ++i)
    {
        pos(i) = (id(i) + 0.5) * resolution_ + origin_(i);
    }
}

int Occupy_map::getOccupancy(Eigen::Vector3d &pos) 
{
    if (!isInMap(pos))
    {
        return -1;
    }
        
    Eigen::Vector3i id;
    posToIndex(pos, id);

    return occupancy_buffer_[id(0) * grid_size_(1) * grid_size_(2) + id(1) * grid_size_(2) + id(2)];
}

double Occupy_map::getCost(Eigen::Vector3d &pos) 
{
    if (!isInMap(pos))
    {
        return -1;
    }
        
    Eigen::Vector3i id;
    posToIndex(pos, id);

    return cost_map_[id(0) * grid_size_(1) * grid_size_(2) + id(1) * grid_size_(2) + id(2)];
}

int Occupy_map::getOccupancy(Eigen::Vector3i &id) 
{
    if (id(0) < 0 || id(0) >= grid_size_(0) || id(1) < 0 || id(1) >= grid_size_(1) || id(2) < 0 ||
        id(2) >= grid_size_(2))
    {
        return -1;
    }
        
    return occupancy_buffer_[id(0) * grid_size_(1) * grid_size_(2) + id(1) * grid_size_(2) + id(2)];
}
}
