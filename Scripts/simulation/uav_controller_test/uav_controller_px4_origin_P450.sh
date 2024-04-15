# 脚本名称: uav_controller_px4_origin
# 脚本描述: 无人机位置环控制器测试

gnome-terminal --window -e 'bash -c "roscore; exec bash"' \
--tab -e 'bash -c "sleep 5; roslaunch prometheus_gazebo sitl_indoor_1uav_P450.launch; exec bash"' \
--tab -e 'bash -c "sleep 6; roslaunch prometheus_uav_control uav_control_main_controller_test.launch pos_controller:=0; exec bash"' \



