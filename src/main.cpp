#include "geometrycentral/surface/manifold_surface_mesh.h"
#include "geometrycentral/surface/meshio.h"
#include "geometrycentral/surface/vertex_position_geometry.h"

#include "geometrycentral/surface/direction_fields.h"
#include "geometrycentral/surface/exact_geodesics.h"

#include "polyscope/polyscope.h"
#include "polyscope/surface_mesh.h"

#include "args/args.hxx"
#include "imgui.h"

#include <fstream>
#include <iomanip>

using namespace geometrycentral;
using namespace geometrycentral::surface;

// == Geometry-central data
std::unique_ptr<ManifoldSurfaceMesh> mesh;
std::unique_ptr<VertexPositionGeometry> geometry;

// Polyscope visualization handle, to quickly add data to the surface
polyscope::SurfaceMesh *psMesh;

// Some algorithm parameters
// double planeHeight = 0.0;
double planeHeight = -0.975;
double planeThreshold = 0.01;
Vector3 planeNormal{0, 1, 0};  // Y-axis as default normal

// Store geodesic distances for export
VertexData<double> currentGeodesicDistances;

// Export geodesic distances to text file
void exportGeodesicDistances(const std::string& filename) {
  if (currentGeodesicDistances.size() == 0) {
    polyscope::warning("No geodesic distances computed yet. Please compute geodesics first.");
    return;
  }
  
  std::ofstream outFile(filename);
  if (!outFile.is_open()) {
    polyscope::error("Failed to open file for writing: " + filename);
    return;
  }
  
  // Write header
  outFile << "# Geodesic distances from plane" << std::endl;
  outFile << "# Plane normal: " << planeNormal.x << " " << planeNormal.y << " " << planeNormal.z << std::endl;
  outFile << "# Plane height: " << planeHeight << std::endl;
  outFile << "# Number of vertices: " << mesh->nVertices() << std::endl;
  outFile << "#" << std::endl;
  outFile << "# Format: vertex_index x y z geodesic_distance" << std::endl;
  outFile << std::endl;
  
  // Write vertex data with positions and distances
  outFile << std::fixed << std::setprecision(10);
  size_t vIdx = 0;
  for (Vertex v : mesh->vertices()) {
    Vector3 pos = geometry->inputVertexPositions[v];
    double distance = currentGeodesicDistances[v];
    outFile << vIdx << " " 
            << pos.x << " " 
            << pos.y << " " 
            << pos.z << " " 
            << distance << std::endl;
    vIdx++;
  }
  
  outFile.close();
  polyscope::info("Exported geodesic distances to: " + filename);
}

// Compute and visualize exact geodesic distances from a plane source
void computeGeodesics() {
  // Find all edges that intersect with the plane
  std::vector<SurfacePoint> sourcePoints;
  EdgeData<double> edgeIndicator(*mesh, 0.0);

  std::cout << "planeHeight: " << planeHeight << std::endl;
  
  for (Edge e : mesh->edges()) {
    Halfedge he = e.halfedge();
    Vertex v1 = he.vertex();
    Vertex v2 = he.twin().vertex();
    
    Vector3 pos1 = geometry->inputVertexPositions[v1];
    Vector3 pos2 = geometry->inputVertexPositions[v2];
    
    // Calculate signed distances from vertices to plane
    double dist1 = dot(pos1, planeNormal) - planeHeight;
    double dist2 = dot(pos2, planeNormal) - planeHeight;
    
    // Check if edge crosses the plane (vertices on opposite sides)
    if (dist1 * dist2 <= 0 && std::abs(dist1 - dist2) > 1e-10) {
      // Calculate intersection parameter t
      double t = dist1 / (dist1 - dist2);
      t = std::max(0.0, std::min(1.0, t)); // Clamp to [0,1]
      
      // Create a SurfacePoint on this edge at the intersection
      SurfacePoint intersectionPoint(e, t);
      sourcePoints.push_back(intersectionPoint);
      edgeIndicator[e] = 1.0;
    }
  }
  
  if (sourcePoints.empty()) {
    polyscope::warning("No edges intersect with the plane! Adjust plane parameters.");
    return;
  }
  
  // Compute exact geodesic distances from the source points
  GeodesicAlgorithmExact geodesicAlg(*mesh, *geometry);
  geodesicAlg.propagate(sourcePoints);
  
  // Get distance to each vertex
  VertexData<double> distances(*mesh);
  for (Vertex v : mesh->vertices()) {
    SurfacePoint vPoint(v);
    auto result = geodesicAlg.closestSource(vPoint);
    distances[v] = result.second;
  }
  
  // Store distances for export
  currentGeodesicDistances = distances;
  
  // Visualize the geodesic distances as a scalar field
  psMesh->addVertexScalarQuantity("geodesic_distance", distances);
  
  // Highlight the source edges
  psMesh->addEdgeScalarQuantity("source_edges", edgeIndicator);
  
  polyscope::info("Computed geodesic distances from plane with " + 
                  std::to_string(sourcePoints.size()) + " edge intersection points");
}

// A user-defined callback, for creating control panels (etc)
// Use ImGUI commands to build whatever you want here, see
// https://github.com/ocornut/imgui/blob/master/imgui.h
void myCallback() {
  static char exportFilename[256] = "geodesic_distances.txt";
  
  ImGui::Text("Geodesic Distance from Plane");
  
  ImGui::Separator();
  ImGui::Text("Plane Parameters:");
  
  // Plane normal direction
  ImGui::Text("Plane Normal Direction:");
  float normal[3] = {(float)planeNormal.x, (float)planeNormal.y, (float)planeNormal.z};
  if (ImGui::InputFloat3("Normal", normal)) {
    planeNormal = Vector3{normal[0], normal[1], normal[2]};
    planeNormal = normalize(planeNormal);
  }
  
  // Plane height/offset
  float planeHeightFloat = (float)planeHeight;
  if (ImGui::SliderFloat("Plane Height", &planeHeightFloat, -1.0f, 1.0f)) {
    planeHeight = planeHeightFloat;
  }
  
  // Threshold for selecting vertices near plane
  float planeThresholdFloat = (float)planeThreshold;
  if (ImGui::SliderFloat("Plane Thickness", &planeThresholdFloat, 0.001f, 0.1f)) {
    planeThreshold = planeThresholdFloat;
  }
  
  ImGui::Separator();
  
  if (ImGui::Button("Compute Geodesics from Plane")) {
    computeGeodesics();
  }
  
  // Quick presets for common plane orientations
  ImGui::Separator();
  ImGui::Text("Quick Plane Presets:");
  if (ImGui::Button("XY Plane (Z-normal)")) {
    planeNormal = Vector3{0, 0, 1};
    computeGeodesics();
  }
  ImGui::SameLine();
  if (ImGui::Button("XZ Plane (Y-normal)")) {
    planeNormal = Vector3{0, 1, 0};
    computeGeodesics();
  }
  ImGui::SameLine();
  if (ImGui::Button("YZ Plane (X-normal)")) {
    planeNormal = Vector3{1, 0, 0};
    computeGeodesics();
  }
  
  // Export functionality
  ImGui::Separator();
  ImGui::Text("Export Geodesic Distances:");
  ImGui::InputText("Filename", exportFilename, sizeof(exportFilename));
  if (ImGui::Button("Export to Text File")) {
    exportGeodesicDistances(std::string(exportFilename));
  }
}

int main(int argc, char **argv) {

  // Configure the argument parser
  args::ArgumentParser parser("geometry-central & Polyscope example project");
  args::Positional<std::string> inputFilename(parser, "mesh", "A mesh file.");

  // Parse args
  try {
    parser.ParseCLI(argc, argv);
  } catch (args::Help &h) {
    std::cout << parser;
    return 0;
  } catch (args::ParseError &e) {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    return 1;
  }

  // If no mesh name was given, use bunny.obj as default
  std::string meshPath;
  if (!inputFilename) {
    meshPath = "bunny.obj";
    std::cout << "No mesh file specified, loading default: " << meshPath << std::endl;
  } else {
    meshPath = args::get(inputFilename);
  }

  // Initialize polyscope
  polyscope::init();

  // Set the callback function
  polyscope::state::userCallback = myCallback;

  // Load mesh
  std::tie(mesh, geometry) = readManifoldSurfaceMesh(meshPath);

  // Register the mesh with polyscope
  psMesh = polyscope::registerSurfaceMesh(
      polyscope::guessNiceNameFromPath(meshPath),
      geometry->inputVertexPositions, mesh->getFaceVertexList(),
      polyscopePermutations(*mesh));

  // Compute initial geodesics from vertex 0
  computeGeodesics();

  // Give control to the polyscope gui
  polyscope::show();

  return EXIT_SUCCESS;
}
