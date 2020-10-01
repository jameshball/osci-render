package engine;

import com.mokiat.data.front.parser.*;

import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

public class WorldObject {
  private List<Vector3> vertices;
  private List<Integer> edgeData;
  private Vector3 position;
  private Vector3 rotation;

  public WorldObject(String filename) {
    this.vertices = new ArrayList<>();
    this.edgeData = new ArrayList<>();
    this.position = new Vector3();
    this.rotation = new Vector3();

    loadFromFile(filename);
  }

  public WorldObject(String filename, Vector3 position, Vector3 rotation) {
    this.vertices = new ArrayList<>();
    this.edgeData = new ArrayList<>();
    this.position = position;
    this.rotation = rotation;

    loadFromFile(filename);
  }

  public WorldObject(List<Vector3> vertices, List<Integer> edgeData, Vector3 position, Vector3 rotation) {
    this.vertices = vertices;
    this.edgeData = edgeData;
    this.position = position;
    this.rotation = rotation;
  }

  public void rotate(Vector3 theta) {
    rotation = rotation.add(theta);
  }

  public void resetRotation() {
    rotation = new Vector3();
  }

  public List<Vector3> getVertices() {
    List<Vector3> newVertices = new ArrayList<>();

    for (Vector3 vertex : vertices) {
      newVertices.add(vertex.rotate(rotation).add(position));
    }

    return newVertices;
  }

  public List<Integer> getEdgeData() {
    return edgeData;
  }

  private void loadFromFile(String filename) {
    try (InputStream in = new FileInputStream(filename)) {
      final IOBJParser parser = new OBJParser();
      final OBJModel model = parser.parse(in);

      for (OBJVertex vertex : model.getVertices()) {
        vertices.add(new Vector3(vertex.x, vertex.y, vertex.z));
      }

      for (OBJObject object : model.getObjects()) {
        for (OBJMesh mesh : object.getMeshes()) {
          for (OBJFace face : mesh.getFaces()) {
            List<OBJDataReference> references = face.getReferences();

            for (int i = 0; i < references.size(); i++) {
              edgeData.add(references.get(i).vertexIndex);
              edgeData.add(references.get((i + 1) % references.size()).vertexIndex);
            }
          }
        }
      }
    } catch (IOException e) {
      e.printStackTrace();
      throw new IllegalArgumentException("Cannot load mesh data from: " + filename);
    }
  }

  public WorldObject clone() {
    return new WorldObject(new ArrayList<>(vertices), new ArrayList<>(edgeData), position, rotation);
  }
}
