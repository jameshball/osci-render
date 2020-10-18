package shapes;

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

public class Shapes {

  public static List<Shape> generatePolygram(int sides, int angleJump, Vector2 start,
      double weight) {
    List<Shape> polygon = new ArrayList<>();

    double theta = angleJump * 2 * Math.PI / sides;
    Vector2 rotated = start.rotate(theta);
    polygon.add(new Line(start, rotated, weight));

    while (!rotated.equals(start)) {
      polygon.add(new Line(rotated.copy(), rotated.rotate(theta), weight));

      rotated = rotated.rotate(theta);
    }

    return polygon;
  }

  public static List<Shape> generatePolygram(int sides, int angleJump, Vector2 start) {
    return generatePolygram(sides, angleJump, start, Line.DEFAULT_WEIGHT);
  }

  public static List<Shape> generatePolygram(int sides, int angleJump, double scale,
      double weight) {
    return generatePolygram(sides, angleJump, new Vector2(scale, scale), weight);
  }

  public static List<Shape> generatePolygram(int sides, int angleJump, double scale) {
    return generatePolygram(sides, angleJump, new Vector2(scale, scale));
  }

  public static List<Shape> generatePolygon(int sides, Vector2 start, double weight) {
    return generatePolygram(sides, 1, start, weight);
  }

  public static List<Shape> generatePolygon(int sides, Vector2 start) {
    return generatePolygram(sides, 1, start);
  }

  public static List<Shape> generatePolygon(int sides, double scale, double weight) {
    return generatePolygon(sides, new Vector2(scale, scale), weight);
  }

  public static List<Shape> generatePolygon(int sides, double scale) {
    return generatePolygon(sides, new Vector2(scale, scale));
  }

  public static double totalLength(List<? extends Shape> shapes) {
    return shapes
        .stream()
        .map(Shape::getLength)
        .reduce(Double::sum)
        .orElse(0d);
  }

  // Performs chinese postman on the input lines to get a path that will render cleanly on the
  // oscilloscope.
  // TODO: Speed up.
  public static List<Shape> sortLines(List<Line> lines) {
    Graph<Vector2, DefaultWeightedEdge> graph = new DefaultUndirectedWeightedGraph<>(
        DefaultWeightedEdge.class);

    // Add all lines in frame to graph as vertices and edges. Edge weight is determined by the
    // length of the line as this is directly proportional to draw time.
    for (Line line : lines) {
      graph.addVertex(line.getA());
      graph.addVertex(line.getB());

      DefaultWeightedEdge edge = new DefaultWeightedEdge();
      graph.addEdge(line.getA(), line.getB(), edge);
      graph.setEdgeWeight(edge, line.length);
    }

    ConnectivityInspector<Vector2, DefaultWeightedEdge> inspector = new ConnectivityInspector<>(
        graph);

    List<Shape> sortedLines = new ArrayList<>();

    // Chinese Postman can only be performed on connected graphs, so iterate over all connected
    // sub-graphs.
    for (Set<Vector2> vertices : inspector.connectedSets()) {
      AsSubgraph<Vector2, DefaultWeightedEdge> subgraph = new AsSubgraph<>(graph, vertices);
      ChinesePostman<Vector2, DefaultWeightedEdge> cp = new ChinesePostman<>();
      Collection<DefaultWeightedEdge> path;

      try {
        path = cp.getCPPSolution(subgraph).getEdgeList();
      } catch (Exception e) {
        // Safety in case getCPPSolution fails.
        path = subgraph.edgeSet();
      }

      for (DefaultWeightedEdge edge : path) {
        sortedLines.add(new Line(subgraph.getEdgeSource(edge), subgraph.getEdgeTarget(edge)));
      }
    }

    return sortedLines;
  }
}
