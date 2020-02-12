package engine;

import com.mokiat.data.front.parser.*;

import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

public class Mesh {
  private List<Vector3> vertices;
  private List<Integer> edgeData;

  public Mesh(List<Vector3> vertices, List<Integer> edgeData) {
    this.vertices = vertices;
    this.edgeData = edgeData;
  }

  public Mesh() {
    this(new ArrayList<>(), new ArrayList<>());
  }

  public List<Vector3> getVertices() {
    return vertices;
  }

  public List<Integer> getEdgeData() {
    return edgeData;
  }

  public static Mesh loadFromFile(String filename) {
    List<Vector3> vertices = new ArrayList<>();
    List<Integer> edgeData = new ArrayList<>();

    // Open a stream to your OBJ resource
    try (InputStream in = new FileInputStream(filename)) {
      // Create an OBJParser and parse the resource
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
      System.err.println("Cannot load mesh data from: " + filename);
      return new Mesh();
    }

    return new Mesh(vertices, edgeData);
  }
}
