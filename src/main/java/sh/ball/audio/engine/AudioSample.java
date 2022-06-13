package sh.ball.audio.engine;

import java.util.function.Consumer;

// defines the different kinds of support audio samples for different audio
// devices
public enum AudioSample {
  UINT8(8, false),
  INT8(8, true),
  INT16(16, true),
  INT24(24, true),
  INT32(32, true),
  FLOAT32(32, true),
  FLOAT64(64, true);

  public final int size;
  public final boolean signed;

  AudioSample(int size, boolean signed) {
    this.size = size;
    this.signed = signed;
  }

  public static void writeAudioSample(AudioSample sample, double[] channels, Consumer<Byte> writeByte) {
    switch (sample) {
      case UINT8 -> {
        for (double channel : channels) {
          writeByte.accept((byte) ((int) (127 * (channel + 1))));
        }
      }
      case INT8 -> {
        for (double channel : channels) {
          writeByte.accept((byte) ((int) (127 * channel)));
        }
      }
      case INT16 -> {
        for (double channel : channels) {
          short shortChannel = (short) (channel * Short.MAX_VALUE);
          writeByte.accept((byte) shortChannel);
          writeByte.accept((byte) (shortChannel >> 8));
        }
      }
      case INT24 -> {
        for (double channel : channels) {
          int intChannel = (int) (channel * 8388606);
          writeByte.accept((byte) intChannel);
          writeByte.accept((byte) (intChannel >> 8));
          writeByte.accept((byte) (intChannel >> 16));
        }
      }
      case INT32 -> {
        for (double channel : channels) {
          int intChannel = (int) (channel * Integer.MAX_VALUE);
          writeByte.accept((byte) intChannel);
          writeByte.accept((byte) (intChannel >> 8));
          writeByte.accept((byte) (intChannel >> 16));
          writeByte.accept((byte) (intChannel >> 24));
        }
      }
      case FLOAT32 -> {
        for (double channel : channels) {
          int intChannel = Float.floatToIntBits((float) channel);
          writeByte.accept((byte) intChannel);
          writeByte.accept((byte) (intChannel >> 8));
          writeByte.accept((byte) (intChannel >> 16));
          writeByte.accept((byte) (intChannel >> 24));
        }
      }
      case FLOAT64 -> {
        for (double channel : channels) {
          long longChannel = Double.doubleToLongBits(channel);
          writeByte.accept((byte) longChannel);
          writeByte.accept((byte) (longChannel >> 8));
          writeByte.accept((byte) (longChannel >> 16));
          writeByte.accept((byte) (longChannel >> 24));
          writeByte.accept((byte) (longChannel >> 32));
          writeByte.accept((byte) (longChannel >> 40));
          writeByte.accept((byte) (longChannel >> 48));
          writeByte.accept((byte) (longChannel >> 56));
        }
      }
    }
  }
}
