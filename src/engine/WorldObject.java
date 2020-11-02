package engine;

import com.mokiat.data.front.parser.*;

import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

public class WorldObject {

  private final List<Vector3> vertices;
  private final List<Integer> edgeData;
  private Vector3 position;
  private Vector3 rotation;

  public WorldObject(List<Vector3> vertices, List<Integer> edgeData, Vector3 position,
      Vector3 rotation) {
    this.vertices = vertices;
    this.edgeData = edgeData;
    this.position = position;
    this.rotation = rotation;
  }

  public WorldObject(String filename, Vector3 position, Vector3 rotation) throws IOException {
    this(new ArrayList<>(), new ArrayList<>(), position, rotation);
    loadFromFile(filename);
  }

  public WorldObject(String filename) throws IOException {
    this(filename, new Vector3(), new Vector3());
  }


  public void rotate(Vector3 theta) {
    rotation = rotation.add(theta);
  }

  public void resetRotation() {
    rotation = new Vector3();
  }

  public void move(Vector3 translation) {
    position = position.add(translation);
  }

  public void resetPosition() {
    position = new Vector3();
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

  private void loadFromFile(String filename) throws IOException {
    InputStream in = new FileInputStream(filename);
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
  }

  public WorldObject clone() {
    return new WorldObject(new ArrayList<>(vertices), new ArrayList<>(edgeData), position,
        rotation);
  }
}
