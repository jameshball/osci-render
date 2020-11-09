package engine;

import com.mokiat.data.front.parser.*;

import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Set;
import org.jgrapht.Graph;
import org.jgrapht.alg.connectivity.ConnectivityInspector;
import org.jgrapht.alg.cycle.ChinesePostman;
import org.jgrapht.graph.AsSubgraph;
import org.jgrapht.graph.DefaultUndirectedWeightedGraph;
import org.jgrapht.graph.DefaultWeightedEdge;

public class WorldObject {

  private final List<Vector3> vertices;

  // These should be a path of vertices from the above vertex list.
  private List<Vector3> vertexPath;
  private Vector3 position;
  private Vector3 rotation;

  private WorldObject(List<Vector3> vertices, List<Vector3> vertexPath, Vector3 position, Vector3 rotation) {
    this.vertices = vertices;
    this.position = position;
    this.rotation = rotation;
    this.vertexPath = vertexPath;
  }

  private WorldObject(List<Vector3> vertices, Vector3 position, Vector3 rotation) {
    this(vertices, new ArrayList<>(), position, rotation);
  }

  public WorldObject(String filename, Vector3 position, Vector3 rotation) throws IOException {
    this(new ArrayList<>(), position, rotation);
    this.vertexPath = getDrawPath(loadFromFile(filename));
  }

  public WorldObject(String filename) throws IOException {
    this(filename, new Vector3(), new Vector3());
  }

  public List<Vector3> getVertexPath() {
    List<Vector3> newVertices = new ArrayList<>();

    for (Vector3 vertex : vertexPath) {
      newVertices.add(vertex.rotate(rotation).add(position));
    }

    return newVertices;
  }

  public List<Vector3> getDrawPath(List<Integer> edgeData) {
    Graph<Vector3, DefaultWeightedEdge> graph = new DefaultUndirectedWeightedGraph<>(
        DefaultWeightedEdge.class);

    List<Vector3> vertexPath = new ArrayList<>();

    // Add all lines in frame to graph as vertices and edges. Edge weight is determined by the
    // length of the line as this is directly proportional to draw time.
    for (int i = 0; i < edgeData.size(); i += 2) {
      Vector3 start = vertices.get(edgeData.get(i));
      Vector3 end = vertices.get(edgeData.get(i + 1));
      graph.addVertex(start);
      graph.addVertex(end);

      DefaultWeightedEdge edge = new DefaultWeightedEdge();
      graph.addEdge(start, end, edge);
      graph.setEdgeWeight(edge, start.distance(end));
    }

    ConnectivityInspector<Vector3, DefaultWeightedEdge> inspector = new ConnectivityInspector<>(
        graph);

    // Chinese Postman can only be performed on connected graphs, so iterate over all connected
    // sub-graphs.
    for (Set<Vector3> vertices : inspector.connectedSets()) {
      AsSubgraph<Vector3, DefaultWeightedEdge> subgraph = new AsSubgraph<>(graph, vertices);
      ChinesePostman<Vector3, DefaultWeightedEdge> cp = new ChinesePostman<>();
      Collection<DefaultWeightedEdge> path;

      try {
        path = cp.getCPPSolution(subgraph).getEdgeList();
      } catch (Exception e) {
        // Safety in case getCPPSolution fails.
        path = subgraph.edgeSet();
      }

      for (DefaultWeightedEdge edge : path) {
        vertexPath.add(subgraph.getEdgeSource(edge));
        vertexPath.add(subgraph.getEdgeTarget(edge));
      }
    }

    return vertexPath;
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

  private List<Integer> loadFromFile(String filename) throws IOException {
    InputStream in = new FileInputStream(filename);
    final IOBJParser parser = new OBJParser();
    final OBJModel model = parser.parse(in);

    List<Integer> edgeData = new ArrayList<>();

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

    return edgeData;
  }

  public WorldObject clone() {
    return new WorldObject(new ArrayList<>(vertices), new ArrayList<>(vertexPath), position, rotation);
  }
}
