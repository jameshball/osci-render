package engine;

import shapes.Line;
import shapes.Vector2;

import java.util.ArrayList;
import java.util.List;

public class Camera extends Renderer{
  private double focalLength;
  private double clipping = 0.001;
  private Vector3 pos;

  public Camera(double focalLength, Vector3 pos) {
    this.focalLength = focalLength;
    this.pos = pos;
  }

  public List<Line> draw(WorldObject worldObject) {
    List<Vector2> vertices = new ArrayList<>();

    for(Vector3 vertex : worldObject.getVertices()) {
      vertices.add(project(vertex));
    }

    return getFrame(vertices, worldObject.getEdgeData());
  }

  private Vector2 project(Vector3 vertex) {
    if(vertex.getZ() - pos.getZ() < clipping) {
      return new Vector2(0, 0);
    }

    return new Vector2(
      vertex.getX() * focalLength / (vertex.getZ() - pos.getZ()) + pos.getX(),
      vertex.getY() * focalLength / (vertex.getZ() - pos.getZ()) + pos.getY()
    );
  }
}
