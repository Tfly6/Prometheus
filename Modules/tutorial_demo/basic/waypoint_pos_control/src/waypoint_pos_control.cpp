/******************************************************************************
 *例程简介: 讲解如何调用uav_control的接口实现无人机的经纬度以及相对高度控制
 *
 *效果说明: 无人机起飞移动到目标经纬度以及相对高度位置点,悬停30秒后降落
 *
 *备注:该例程仅支持Prometheus仿真,真机测试需要熟练掌握相关接口的定义后以及真机适配修改后使用
 ******************************************************************************/

#include <ros/ros.h>
#include <prometheus_msgs/UAVCommand.h>
#include <prometheus_msgs/UAVState.h>
#include <prometheus_msgs/UAVControlState.h>
#include <mavros_msgs/Waypoint.h>
#include <mavros_msgs/WaypointList.h>
#include <unistd.h>
#include "printf_utils.h"
#include <cmath>

using namespace std;

// 创建无人机相关数据变量
prometheus_msgs::UAVCommand uav_command;
prometheus_msgs::UAVState uav_state;
prometheus_msgs::UAVControlState uav_control_state;
mavros_msgs::WaypointList waypoint_list;
constexpr double kEarthRadius = 6371000.0; // 地球半径，单位：米

// 将角度从度数转换为弧度
double toRadians(double degrees) {
    return degrees * M_PI / 180.0;
}
// 无人机状态回调函数
void uav_state_cb(const prometheus_msgs::UAVState::ConstPtr &msg)
{
    uav_state = *msg;
}

// 无人机控制状态回调函数
void uav_control_state_cb(const prometheus_msgs::UAVControlState::ConstPtr &msg)
{
    uav_control_state = *msg;
}

void uav_set_waypoints_cb(const mavros_msgs::WaypointList::ConstPtr &msg)
{
    waypoint_list = *msg;
    waypoint_list.current_seq = 0;
}

// 使用Haversine公式计算球面上两点之间的距离
double calculateDistance(double uav_lat, double uav_long, double uav_alt,
                         double pos_lat, double pos_long, double pos_alt) {
    double lat1 = toRadians(uav_lat);
    double lon1 = toRadians(uav_long);
    double lat2 = toRadians(pos_lat);
    double lon2 = toRadians(pos_long);

    double dLat = lat2 - lat1;
    double dLon = lon2 - lon1;

    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1) * std::cos(lat2) *
               std::sin(dLon / 2) * std::sin(dLon / 2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));

    double distance = kEarthRadius * c;
    return distance;
}

// 计算无人机的航向角
double calculateHeading(double uav_lat, double uav_lon, double target_lat, double target_lon) {
    // 将经纬度转换为弧度
    double lat1 = toRadians(uav_lat);
    double lon1 = toRadians(uav_lon);
    double lat2 = toRadians(target_lat);
    double lon2 = toRadians(target_lon);

    // 计算目标点相对于无人机的直角坐标差值
    double x = std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(lon2 - lon1);
    double y = std::sin(lon2 - lon1) * std::cos(lat2);

    // 计算方位角（azimuth angle）
    double azimuth = std::atan2(y, x);

    // 转换为以ENU坐标系下的弧度
    azimuth = -std::fmod(azimuth + 2 * M_PI, 2 * M_PI) + M_PI/2;

    return azimuth;
}

// 主函数
int main(int argc, char **argv)
{
    // ROS初始化,设定节点名
    ros::init(argc, argv, "waypoint_pos_control");
    // 创建句柄
    ros::NodeHandle n;

    // 声明起飞高度,无人机id变量
    float takeoff_height;
    int uav_id;
    // 设定一个阈值，表示到达航点的距离阈值
    double threshold;  // 以米为单位
    // 获取起飞高度参数
    ros::param::get("/uav_control_main_1/control/Takeoff_height", takeoff_height);
    ros::param::get("~uav_id", uav_id);
    ros::param::get("~threshold",threshold);

    // 创建无人机控制命令发布者
    ros::Publisher uav_command_pub = n.advertise<prometheus_msgs::UAVCommand>("/uav" + std::to_string(uav_id) + "/prometheus/command", 10);
    // 创建无人机状态命令订阅者
    ros::Subscriber uav_state_sub = n.subscribe<prometheus_msgs::UAVState>("/uav" + std::to_string(uav_id) + "/prometheus/state", 10, uav_state_cb);
    // 创建无人机控制状态命令订阅者
    ros::Subscriber uav_control_state_sub = n.subscribe<prometheus_msgs::UAVControlState>("/uav" + std::to_string(uav_id) + "/prometheus/control_state", 10, uav_control_state_cb);
    // 创建无人机航点订阅者
    ros::Subscriber waypoints_list_sub = n.subscribe<mavros_msgs::WaypointList>("/uav" + std::to_string(uav_id) + "/prometheus/set_waypoints", 10, uav_set_waypoints_cb);
    // 循环频率设置为1HZ
    ros::Rate r(1);
    // 创建命令发布标志位,命令发布则为true;初始化为false
    bool cmd_pub_flag = false;

    // 固定的浮点显示
    cout.setf(ios::fixed);
    // setprecision(n) 设显示小数精度为2位
    cout << setprecision(2);
    // 左对齐
    cout.setf(ios::left);
    // 强制显示小数点
    cout.setf(ios::showpoint);
    // 强制显示符号
    cout.setf(ios::showpos);

    // 打印demo相关信息
    cout << GREEN << " [Waypoint position control] tutorial_demo start " << TAIL << endl;
    sleep(1);
    cout << GREEN << " Level: [Basic] " << TAIL << endl;
    sleep(1);
    cout << GREEN << " Please use the RC SWA to armed, and the SWB to switch the drone to [COMMAND_CONTROL] mode  " << TAIL << endl;

    waypoint_list.current_seq = 0;
    int point_count = 0;

    while (ros::ok())
    {
        // 调用一次回调函数
        ros::spinOnce();
        // 检测无人机是否处于[COMMAND_CONTROL]模式
        if (uav_control_state.control_state == prometheus_msgs::UAVControlState::COMMAND_CONTROL)
        {
            // 检测控制命令是否发布,没有发布则进行命令的发布
            if (!cmd_pub_flag)
            {
                if (waypoint_list.waypoints.empty())
                {
                    cout << YELLOW << " Waiting waypoint publish " << TAIL << endl;
                    r.sleep();
                    continue;
                }
                point_count = waypoint_list.waypoints.size();
                if (waypoint_list.current_seq < point_count)
                {
                    // 时间戳
                    uav_command.header.stamp = ros::Time::now();
                    // 坐标系
                    uav_command.header.frame_id = "WGS84";
                    // Move模式
                    uav_command.Agent_CMD = prometheus_msgs::UAVCommand::Move;
                    // Move_mode
                    uav_command.Move_mode = prometheus_msgs::UAVCommand::LAT_LON_ALT;
                    // 在无人机当前经纬度飞到5米高度
                    uav_command.latitude = waypoint_list.waypoints[waypoint_list.current_seq].x_lat;
                    uav_command.longitude = waypoint_list.waypoints[waypoint_list.current_seq].y_long;
                    uav_command.altitude = waypoint_list.waypoints[waypoint_list.current_seq].z_alt;
                    uav_command.yaw_ref = calculateHeading(uav_state.latitude,uav_state.longitude,uav_command.latitude,uav_command.longitude);
                    // 发布的命令ID加1
                    uav_command.Command_ID += 1;
                    // 发布降落命令
                    uav_command_pub.publish(uav_command);
                    // 命令发布标志位置为true
                    cmd_pub_flag = true;

                    // 打印无人机控制命令发布信息
                    cout << GREEN << " [LAT_LON_ALT] command publish " << uav_command.latitude << "," << uav_command.longitude << "," << uav_command.altitude << TAIL << endl;
                    cout << GREEN << " [YAW] command publish " << uav_command.yaw_ref << TAIL << endl;
                }else
                {
                    waypoint_list.waypoints.clear();
                    waypoint_list.current_seq = 0;
                }
            }
            else
            {
                // 计算无人机与航点之间的距离
                double distance = calculateDistance(uav_state.latitude,uav_state.longitude,uav_state.position[2],uav_command.latitude,uav_command.longitude,uav_command.altitude);
                // 当无人机跟目标点的位置小于等于阈值范围内时认为任务完成并准备下一个目标点
                if (distance <= threshold)
                {
                    cout << GREEN << " Point " << waypoint_list.current_seq + 1 << " success arrived " << TAIL << endl;
                    sleep(1);
                    waypoint_list.current_seq++;
                    cmd_pub_flag = false;
                }
                else
                {
                    // 打印当前距离目标点位置
                    cout << GREEN << " Distance from target point: " << distance << TAIL << endl;
                }
            }
        }
        else
        {
            // 在控制命令发布后,但无人机未结束任务的情况下,此时无人机未处于[COMMAND_CONTROL]控制状态,认为无人机出现意外情况,任务中止
            if (cmd_pub_flag)
            {
                cout << RED << " Unknown error! [Global position control] tutorial_demo aborted" << TAIL << endl;
            }
            // 命令未发布,等待无人机进入[COMMAND_CONTROL]状态
            else
            {
                cout << YELLOW << " Wait for UAV to enter [COMMAND_CONTROL] MODE " << TAIL << endl;
            }
        }
        r.sleep();
    }
    return 0;
}