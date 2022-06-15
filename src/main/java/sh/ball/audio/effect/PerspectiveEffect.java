package sh.ball.audio.effect;

import sh.ball.engine.Vector3;
import sh.ball.shapes.Vector2;

// 3D rotation effect
public class PerspectiveEffect extends PhaseEffect implements SettableEffect {

  private double focalLength = 1.0;
  private double zPos = 1.0;

  public PerspectiveEffect(int sampleRate, double speed) {
    super(sampleRate, speed);
  }

  public PerspectiveEffect(int sampleRate) {
    this(sampleRate, 1);
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    Vector3 vertex = new Vector3(vector.x, vector.y, 0.0);
    double theta = nextTheta();
    vertex = vertex.rotate(new Vector3(0, theta, theta));

    return new Vector2(
      vertex.x * focalLength / (vertex.z - zPos),
      vertex.y * focalLength / (vertex.z - zPos)
    );
  }

  @Override
  public void setValue(double value) {
    zPos = 1.0 + (value - 0.5) * 3;
  }
}
