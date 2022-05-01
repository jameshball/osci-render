package sh.ball.engine;

import com.mokiat.data.front.parser.*;

import java.io.IOException;
import java.io.InputStream;
import java.util.*;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import org.jgrapht.Graph;
import org.jgrapht.alg.connectivity.ConnectivityInspector;
import org.jgrapht.alg.cycle.ChinesePostman;
import org.jgrapht.alg.shortestpath.DijkstraShortestPath;
import org.jgrapht.graph.*;

public class WorldObject {

  private final List<Vector3> objVertices;

  // These should be a path of vertices from the above vertex list.
  private float[] triangles;
  private List<List<Vector3>> vertexPath;
  private Vector3 position;
  private Vector3 rotation;
  private boolean hideEdges = false;

  public WorldObject(Vector3[] vertices, int[] edgeIndices, int[][] faceIndices) {
    vertexPath = new ArrayList<>();
    objVertices = Arrays.stream(vertices).toList();
    Set<Line3D> edges = loadFromArrays(vertices, edgeIndices, faceIndices);
    getDrawPath(edges);
  }

  private WorldObject(List<Vector3> objVertices, List<List<Vector3>> vertexPath, Vector3 position,
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

  public List<List<Vector3>> getVertexPath() {
    return vertexPath;
  }

  public float[] getTriangles() {
    return triangles;
  }

  private Vector3 addEdgeToPath(Vector3 realSource, Graph<Vector3, DefaultEdge> graph, DefaultEdge edge, Set<DefaultEdge> visited, Set<DefaultEdge> notVisited, List<Vector3> path) {
    visited.add(edge);
    notVisited.remove(edge);

    Vector3 supposedSource = graph.getEdgeSource(edge);
    Vector3 supposedSink = graph.getEdgeTarget(edge);
    // no guarantee on order since the graph is undirected
    Vector3 source = supposedSource == realSource ? supposedSource : supposedSink;
    Vector3 sink = supposedSink == realSource ? supposedSource : supposedSink;

    path.add(source);
    path.add(sink);

    return sink;
  }

  // Unused but faster than chinese postman - not good at handling with lots of vertices
  private void fasterPath(Graph<Vector3, DefaultEdge> graph, Set<Vector3> vertices) {
    Set<DefaultEdge> visited = new HashSet<>();
    List<Vector3> path = new ArrayList<>();
    AsSubgraph<Vector3, DefaultEdge> subgraph = new AsSubgraph<>(graph, vertices);
    Set<DefaultEdge> notVisited = new HashSet<>(subgraph.edgeSet());

    DefaultEdge outgoingEdge = notVisited.stream().findFirst().orElseThrow();
    Vector3 currentVertex = subgraph.getEdgeSource(outgoingEdge);
    Vector3 startVertex = currentVertex;
    visited.add(outgoingEdge);
    notVisited.remove(outgoingEdge);
    path.add(currentVertex);
    path.add(subgraph.getEdgeTarget(outgoingEdge));

    while (!notVisited.isEmpty()) {
      Set<DefaultEdge> outgoing = subgraph.outgoingEdgesOf(currentVertex);
      DefaultEdge newEdge = null;
      for (DefaultEdge edge : outgoing) {
        if (!visited.contains(edge)) {
          newEdge = edge;
          currentVertex = addEdgeToPath(currentVertex, subgraph, edge, visited, notVisited, path);
          break;
        }
      }

      if (newEdge == null) {
        newEdge = notVisited.stream().findFirst().orElseThrow();
        Vector3 dest = subgraph.getEdgeSource(newEdge);

        List<DefaultEdge> shortestPath = DijkstraShortestPath.findPathBetween(subgraph, currentVertex, dest).getEdgeList();
        for (DefaultEdge edge : shortestPath) {
          addEdgeToPath(currentVertex, subgraph, edge, visited, notVisited, path);
        }

        currentVertex = dest;
      }
    }

    // return back to start vertex
    path.addAll(DijkstraShortestPath.findPathBetween(subgraph, currentVertex, startVertex).getVertexList());
    vertexPath.add(path);
  }

  public void getDrawPath(Set<Line3D> edges) {
    Graph<Vector3, DefaultEdge> graph = new DefaultUndirectedGraph<>(DefaultEdge.class);

    // Add all lines in frame to graph as vertices and edges.
    Stream.concat(
      edges.stream().map(Line3D::getStart),
      edges.stream().map(Line3D::getEnd)
    ).distinct().forEach(graph::addVertex);

    for (Line3D edge : edges) {
      graph.addEdge(edge.getStart(), edge.getEnd());
    }

    ConnectivityInspector<Vector3, DefaultEdge> inspector = new ConnectivityInspector<>(
      graph);

    // Chinese Postman can only be performed on connected graphs, so iterate over all connected
    // sub-graphs.
    vertexPath = inspector.connectedSets().parallelStream().map(vertices -> {
      ChinesePostman<Vector3, DefaultEdge> cp = new ChinesePostman<>();
      AsSubgraph<Vector3, DefaultEdge> subgraph = new AsSubgraph<>(graph, vertices);
      return cp.getCPPSolution(subgraph).getVertexList();
    }).toList();
  }

  public Vector3 getRotation() {
    return rotation;
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

  public Vector3 getPosition() {
    return position;
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

  private Set<Line3D> loadFromArrays(Vector3[] vertices, int[] edgeIndices, int[][] faceIndices) {
    Set<Line3D> edges = new LinkedHashSet<>();

    int numTriangles = 0;

    for (int i = 0; i < edgeIndices.length; i += 2) {
      edges.add(new Line3D(vertices[edgeIndices[i]], vertices[edgeIndices[i + 1]]));
    }

    for (int[] face : faceIndices) {
      numTriangles += face.length - 2;
    }

    triangles = new float[9 * numTriangles];
    int triangle = 0;

    for (int[] face : faceIndices) {
      Vector3 v1 = objVertices.get(face[0]);
      Vector3 curr = null;
      Vector3 prev;

      for (int i = 0; i < face.length - 1; i++) {
        prev = curr;
        curr = objVertices.get(face[i + 1]);
        if (prev != null) {
          triangles[9 * triangle] = (float) v1.x;
          triangles[9 * triangle + 1] = (float) v1.y;
          triangles[9 * triangle + 2] = (float) v1.z;
          triangles[9 * triangle + 3] = (float) prev.x;
          triangles[9 * triangle + 4] = (float) prev.y;
          triangles[9 * triangle + 5] = (float) prev.z;
          triangles[9 * triangle + 6] = (float) curr.x;
          triangles[9 * triangle + 7] = (float) curr.y;
          triangles[9 * triangle + 8] = (float) curr.z;
          triangle++;
        }
      }
    }

    return edges;
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

    // auto-centre around origin
    Vector3 offset = new Vector3();
    double max = 1.0;
    for (OBJVertex vertex : model.getVertices()) {
      offset = offset.add(new Vector3(vertex.x, vertex.y, vertex.z));
      if (Math.abs(vertex.x) > max) {
        max = Math.abs(vertex.x);
      }
      if (Math.abs(vertex.y) > max) {
        max = Math.abs(vertex.y);
      }
      if (Math.abs(vertex.z) > max) {
        max = Math.abs(vertex.z);
      }
    }
    offset = offset.scale(1.0/model.getVertices().size());

    for (OBJVertex vertex : model.getVertices()) {
      // normalise coordinates between
      objVertices.add(new Vector3(vertex.x, vertex.y, vertex.z).sub(offset).scale(1.0/max));
    }

    int numTriangles = 0;

    for (OBJObject object : model.getObjects()) {
      for (OBJMesh mesh : object.getMeshes()) {
        for (OBJFace face : mesh.getFaces()) {
          List<OBJDataReference> references = face.getReferences();
          numTriangles += references.size() - 2;


          for (int i = 0; i < references.size(); i++) {
            edges.add(new Line3D(
              objVertices.get(references.get(i).vertexIndex),
              objVertices.get(references.get((i + 1) % references.size()).vertexIndex)
            ));
          }
        }
      }
    }

    triangles = new float[9 * numTriangles];
    int triangle = 0;

    for (OBJObject object : model.getObjects()) {
      for (OBJMesh mesh : object.getMeshes()) {
        for (OBJFace face : mesh.getFaces()) {
          List<OBJDataReference> references = face.getReferences();
          Vector3 v1 = objVertices.get(references.get(0).vertexIndex);
          Vector3 curr = null;
          Vector3 prev;

          for (int i = 0; i < references.size() - 1; i++) {
            prev = curr;
            curr = objVertices.get(references.get(i + 1).vertexIndex);
            if (prev != null) {
              triangles[9 * triangle] = (float) v1.x;
              triangles[9 * triangle + 1] = (float) v1.y;
              triangles[9 * triangle + 2] = (float) v1.z;
              triangles[9 * triangle + 3] = (float) prev.x;
              triangles[9 * triangle + 4] = (float) prev.y;
              triangles[9 * triangle + 5] = (float) prev.z;
              triangles[9 * triangle + 6] = (float) curr.x;
              triangles[9 * triangle + 7] = (float) curr.y;
              triangles[9 * triangle + 8] = (float) curr.z;
              triangle++;
            }
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

  public void hideEdges(boolean hideEdges) {
    this.hideEdges = hideEdges;
  }

  public boolean edgesHidden() {
    return hideEdges;
  }
}
