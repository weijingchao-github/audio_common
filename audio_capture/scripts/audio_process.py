# extract one_channel and downsample to 16000Hz
import os
import sys

path = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
)
path = os.path.join(path, "devel/lib/python3/dist-packages")
sys.path.insert(0, path)

import numpy as np
import rospy
import scipy
from audio_common_msgs.msg import AudioDataStamped, ProcessedAudioDataStamped


class AudioProcessor:
    def __init__(self):
        self.sub_audio = rospy.Subscriber(
            "audio_stamped", AudioDataStamped, self._audio_cb, queue_size=10
        )
        self.pub_audio = rospy.Publisher(
            "processed_audio_stamped", ProcessedAudioDataStamped, queue_size=10
        )

    def _audio_cb(self, audio_stamped_msg: AudioDataStamped):
        audio_data = np.frombuffer(
            audio_stamped_msg.audio.data, dtype=np.int16
        )  # 之所以是np.int16，因为音频数据是双字节的
        # 确保音频数据是双声道
        assert len(audio_data) % 2 == 0
        # 提取左声道数据
        left_channel_audio = audio_data[::2]
        # 降采样：从48000Hz降为16000Hz
        num_samples = int(len(left_channel_audio) * 16000 / 48000)
        downsampled_audio = (
            scipy.signal.resample(left_channel_audio, num_samples)
            .astype(np.int16)
            .tolist()
        )
        # 创建并发布处理后的消息
        processed_msg = ProcessedAudioDataStamped()
        processed_msg.header = audio_stamped_msg.header
        processed_msg.audio = downsampled_audio
        self.pub_audio.publish(processed_msg)


def main():
    rospy.init_node("audio_processor")
    AudioProcessor()
    rospy.spin()


if __name__ == "__main__":
    main()
