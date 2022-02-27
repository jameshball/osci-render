package sh.ball.engine;

import com.mokiat.data.front.parser.*;

import java.io.IOException;
import java.io.InputStream;
import java.util.*;

import org.jgrapht.Graph;
import org.jgrapht.alg.connectivity.ConnectivityInspector;
import org.jgrapht.alg.cycle.ChinesePostman;
import org.jgrapht.graph.AsSubgraph;
import org.jgrapht.graph.DefaultUndirectedWeightedGraph;
import org.jgrapht.graph.DefaultWeightedEdge;

public class WorldObject {

  private final List<Vector3> objVertices;

  // These should be a path of vertices from the above vertex list.
  private List<Vector3> vertexPath;
  private Vector3 position;
  private Vector3 rotation;

  private WorldObject(List<Vector3> objVertices, List<Vector3> vertexPath, Vector3 position,
                      Vector3 rotation) {
    this.objVertices = objVertices;
    this.position = position;
    this.rotation = rotation;
    this.vertexPath = vertexPath;
  }

  private WorldObject(List<Vector3> objVertices, Vector3 position, Vector3 rotation) {
    this(objVertices, new ArrayList<>(), position, rotation);
  }

  public WorldObject(InputStream input, Vector3 position, Vector3 rotation) throws IOException {
    this(new ArrayList<>(), position, rotation);
    getDrawPath(loadFromInput(input));
  }

  public WorldObject(InputStream input) throws IOException {
    this(input, new Vector3(), new Vector3());
  }

  public List<Vector3> getVertexPath() {
    List<Vector3> newVertices = new ArrayList<>();

    for (Vector3 vertex : vertexPath) {
      newVertices.add(vertex.rotate(rotation).add(position));
    }

    return newVertices;
  }

  public void getDrawPath(Set<Line3D> edges) {
    Graph<Vector3, DefaultWeightedEdge> graph = new DefaultUndirectedWeightedGraph<>(
      DefaultWeightedEdge.class);

    vertexPath = new ArrayList<>();

    // Add all lines in frame to graph as vertices and edges. Edge weight is determined by the
    // length of the line as this is directly proportional to draw time.
    for (Line3D edge : edges) {
      graph.addVertex(edge.getStart());
      graph.addVertex(edge.getEnd());

      DefaultWeightedEdge weightedEdge = new DefaultWeightedEdge();
      graph.addEdge(edge.getStart(), edge.getEnd(), weightedEdge);
      graph.addEdge(edge.getStart(), edge.getEnd());
      graph.setEdgeWeight(weightedEdge, edge.getStart().distance(edge.getEnd()));
    }

    ConnectivityInspector<Vector3, DefaultWeightedEdge> inspector = new ConnectivityInspector<>(
      graph);

    // Chinese Postman can only be performed on connected graphs, so iterate over all connected
    // sub-graphs.
    // TODO: parallelize?
    for (Set<Vector3> vertices : inspector.connectedSets()) {
      AsSubgraph<Vector3, DefaultWeightedEdge> subgraph = new AsSubgraph<>(graph, vertices);
      ChinesePostman<Vector3, DefaultWeightedEdge> cp = new ChinesePostman<>();
      List<Vector3> path = cp.getCPPSolution(subgraph).getVertexList();

      for (int i = 1; i < path.size(); i++) {
        vertexPath.add(path.get(i - 1));
        vertexPath.add(path.get(i));
      }
    }
  }

  public void setRotation(Vector3 rotation) {
    this.rotation = rotation;
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

  public Vector3 getVertex(int i) {
    return objVertices.get(i).rotate(rotation).add(position);
  }

  public int numVertices() {
    return objVertices.size();
  }

  private Set<Line3D> loadFromInput(InputStream input) {
    OBJModel model;

    try {
      IOBJParser parser = new OBJParser();
      model = parser.parse(input);
    } catch (IOException e) {
      return Set.of();
    }

    Set<Line3D> edges = new LinkedHashSet<>();

    for (OBJVertex vertex : model.getVertices()) {
      objVertices.add(new Vector3(vertex.x, vertex.y, vertex.z));
    }

    for (OBJObject object : model.getObjects()) {
      for (OBJMesh mesh : object.getMeshes()) {
        for (OBJFace face : mesh.getFaces()) {
          List<OBJDataReference> references = face.getReferences();

          for (int i = 0; i < references.size(); i++) {
            edges.add(new Line3D(
              objVertices.get(references.get(i).vertexIndex),
              objVertices.get(references.get((i + 1) % references.size()).vertexIndex)
            ));
          }
        }
      }
    }

    return edges;
  }

  public WorldObject clone() {
    return new WorldObject(new ArrayList<>(objVertices), new ArrayList<>(vertexPath), position,
      rotation);
  }
}
