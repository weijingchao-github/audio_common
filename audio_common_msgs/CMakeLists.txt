cmake_minimum_required(VERSION 2.8.3)

project(audio_common_msgs)

find_package(catkin REQUIRED
  COMPONENTS
  message_generation
  std_msgs
)

add_message_files(DIRECTORY msg FILES
  AudioData.msg
  AudioDataStamped.msg
  AudioInfo.msg
  ProcessedAudioDataStamped.msg
  )

generate_messages(
  DEPENDENCIES
  std_msgs
)

catkin_package(CATKIN_DEPENDS message_runtime)
