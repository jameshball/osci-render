package engine;

import shapes.Line;
import shapes.Vector2;

import java.util.ArrayList;
import java.util.List;

public class Camera extends Renderer{

  // position at 0,0,0,0
  // rotation towards positive z axis

  private double focalLength;
  private double clipping = 0.001;
  private Vector3 position;
  private double fov;

  public Camera() {
    this.focalLength = 0.6;
    this.position = new Vector3(0,0,-2);
    this.fov = 60;
  }

  public List<Line> draw(WorldObject worldObject) {
    List<Vector2> vertices = new ArrayList<>();
    for(Vector3 vertex : worldObject.getVertices()) {
      vertices.add(this.project(vertex));
    }

    return this.getFrame(vertices, worldObject.getEdgeData());
  }

  private Vector2 project(Vector3 vertex) {
    if(vertex.getZ() - this.position.getZ() < clipping) {
      return new Vector2(0, 0);
    }
    return new Vector2(
      vertex.getX() * focalLength / (vertex.getZ() - this.position.getZ()) + this.position.getX(),
      vertex.getY() * focalLength / (vertex.getZ() - this.position.getZ()) + this.position.getY()
    );
  }
}
