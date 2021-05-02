package sh.ball.parser;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.AbstractList;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.RandomAccess;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;

import org.w3c.dom.Document;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;
import org.xml.sax.SAXException;

public final class XmlUtil {

  private XmlUtil() {
  }

  public static List<Node> asList(NodeList n) {
    return n.getLength() == 0 ?
      Collections.emptyList() : new NodeListWrapper(n);
  }

  static final class NodeListWrapper extends AbstractList<Node>
    implements RandomAccess {

    private final NodeList list;

    NodeListWrapper(NodeList l) {
      list = l;
    }

    public Node get(int index) {
      return list.item(index);
    }

    public int size() {
      return list.getLength();
    }
  }

  public static String getNodeValue(Node node, String namedItem) {
    Node attribute = node.getAttributes().getNamedItem(namedItem);
    return attribute == null ? null : attribute.getNodeValue();
  }

  public static Document getXMLDocument(InputStream input)
    throws IOException, SAXException, ParserConfigurationException {
    // Opens XML reader for svg file.
    DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();

    // Remove validation which massively slows down file parsing
    factory.setNamespaceAware(false);
    factory.setValidating(false);
    factory.setFeature("http://xml.org/sax/features/namespaces", false);
    factory.setFeature("http://xml.org/sax/features/validation", false);
    factory.setFeature("http://apache.org/xml/features/nonvalidating/load-dtd-grammar", false);
    factory.setFeature("http://apache.org/xml/features/nonvalidating/load-external-dtd", false);

    DocumentBuilder builder = factory.newDocumentBuilder();

    return builder.parse(input);
  }

  public static List<Node> getAttributesOnTags(Document xml, String tagName, String attribute) {
    List<Node> attributes = new ArrayList<>();

    for (Node elem : asList(xml.getElementsByTagName(tagName))) {
      attributes.add(elem.getAttributes().getNamedItem(attribute));
    }

    return attributes;
  }
}
