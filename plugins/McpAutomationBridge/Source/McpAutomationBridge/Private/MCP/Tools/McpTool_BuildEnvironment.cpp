#include "McpVersionCompatibility.h"
#include "MCP/McpToolDefinition.h"
#include "MCP/McpToolRegistry.h"
#include "MCP/McpSchemaBuilder.h"
#include "MCP/McpConsolidatedActionRouting.h"

class FMcpTool_BuildEnvironment : public FMcpToolDefinition
{
public:
	FString GetName() const override { return TEXT("build_environment"); }

	FString GetDescription() const override
	{
		return TEXT("Create/sculpt landscapes, paint foliage, and generate procedural "
			"terrain/biomes, modular buildings, and road-aligned city blocks.");
	}

	FString GetCategory() const override { return TEXT("world"); }

	// Pattern A: default GetDispatchAction() returns GetName()

	TSharedPtr<FJsonObject> BuildInputSchema() const override
	{
		return FMcpSchemaBuilder()
			.StringEnum(TEXT("action"), McpConsolidatedActions::BuildEnvironment(),
				TEXT("Action"))
			.String(TEXT("name"), TEXT("Name identifier."))
			.StringEnum(TEXT("buildingType"), { TEXT("house"), TEXT("shop"), TEXT("apartment"), TEXT("office"), TEXT("skyscraper") }, TEXT("Procedural building archetype."))
			.ArrayOfObjects(TEXT("footprintPoints"), TEXT("World-space footprint polygon."))
			.Number(TEXT("floors"), TEXT("Number of storeys."))
			.Number(TEXT("floorHeight"), TEXT("Storey height in Unreal units."))
			.Number(TEXT("wallThickness"), TEXT("Exterior wall thickness."))
			.StringEnum(TEXT("roofType"), { TEXT("flat"), TEXT("gable"), TEXT("hip"), TEXT("mansard") }, TEXT("Roof profile."))
			.String(TEXT("wallMaterial"), TEXT("Wall material asset path."))
			.String(TEXT("windowMaterial"), TEXT("Window material asset path."))
			.String(TEXT("roofMaterial"), TEXT("Roof material asset path."))
			.String(TEXT("trimMaterial"), TEXT("Trim material asset path."))
			.String(TEXT("interiorMaterial"), TEXT("Interior material asset path."))
			.Bool(TEXT("generateDoors"), TEXT("Generate clear entrances."))
			.Bool(TEXT("generateWindows"), TEXT("Generate instanced windows."))
			.Bool(TEXT("generateBalconies"), TEXT("Generate instanced balconies."))
			.Bool(TEXT("generateStorefront"), TEXT("Generate ground-floor storefront."))
			.Bool(TEXT("generateInterior"), TEXT("Generate floors, stairs, rooms and corridors."))
			.Bool(TEXT("useHISM"), TEXT("Use HISM for repeated facade elements."))
			.Bool(TEXT("enableNanite"), TEXT("Report/use Nanite-capable reusable meshes."))
			.Bool(TEXT("generateLODs"), TEXT("Use reusable LOD-ready meshes."))
			.String(TEXT("roadSplineActor"), TEXT("Road spline actor whose clearance must remain open."))
			.Number(TEXT("roadClearance"), TEXT("Minimum road clearance."))
			.String(TEXT("buildingName"), TEXT("Generated building actor label."))
			.String(TEXT("buildingActor"), TEXT("Building actor to inspect/regenerate/save."))
			.String(TEXT("blueprintPath"), TEXT("Destination /Game path for the generated Blueprint."))
			.Number(TEXT("maxBuildings"), TEXT("Maximum buildings for a city block."))
			.String(TEXT("landscapeName"), TEXT(""))
			.Array(TEXT("heightData"), TEXT(""), TEXT("number"))
			.Number(TEXT("minX"), TEXT(""))
			.Number(TEXT("minY"), TEXT(""))
			.Number(TEXT("maxX"), TEXT(""))
			.Number(TEXT("maxY"), TEXT(""))
			.FreeformObject(TEXT("region"), TEXT("Landscape region bounds."))
			.Bool(TEXT("updateNormals"), TEXT(""))
			.Bool(TEXT("skipFlush"), TEXT("Skip editor flush/update when supported."))
			.Object(TEXT("location"), TEXT("3D location (x, y, z)."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.Object(TEXT("rotation"), TEXT("3D rotation (pitch, yaw, roll)."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("pitch")).Number(TEXT("yaw")).Number(TEXT("roll"));
			})
			.Number(TEXT("sizeX"), TEXT(""))
			.Number(TEXT("sizeY"), TEXT(""))
			.Number(TEXT("sectionSize"), TEXT(""))
			.Number(TEXT("sectionsPerComponent"), TEXT(""))
			.Object(TEXT("componentCount"), TEXT("2D vector."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y"));
			})
			.String(TEXT("materialPath"), TEXT("Material asset path."))
			.String(TEXT("tool"), TEXT(""))
			.Number(TEXT("radius"), TEXT(""))
			.Number(TEXT("strength"), TEXT(""))
			.Number(TEXT("falloff"), TEXT(""))
			.String(TEXT("operation"), TEXT("Heightmap or terrain edit operation."))
			.String(TEXT("layerName"), TEXT(""))
			.String(TEXT("actorName"), TEXT("Name of the actor."))
			.String(TEXT("targetActor"), TEXT("Target actor name."))
			.String(TEXT("waterBodyName"), TEXT("Water body actor name."))
			.String(TEXT("foliageType"), TEXT(""))
			.String(TEXT("foliageTypePath"),
				TEXT("Asset path (e.g., /Game/Path/Asset)."))
			.String(TEXT("meshPath"), TEXT("Mesh asset path."))
			.Number(TEXT("density"), TEXT(""))
			.Number(TEXT("minScale"), TEXT(""))
			.Number(TEXT("maxScale"), TEXT(""))
			.Number(TEXT("cullDistance"), TEXT(""))
			.Bool(TEXT("alignToNormal"), TEXT(""))
			.Bool(TEXT("randomYaw"), TEXT(""))
			.Bool(TEXT("removeAll"), TEXT("Explicitly remove all foliage instances."))
			.ArrayOfObjects(TEXT("locations"), TEXT(""))
			.ArrayOfObjects(TEXT("transforms"), TEXT(""))
			.Object(TEXT("position"), TEXT("3D location (x, y, z)."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.FreeformObject(TEXT("bounds"), TEXT(""))
			.String(TEXT("volumeName"), TEXT(""))
			.Number(TEXT("seed"), TEXT(""))
			.ArrayOfObjects(TEXT("foliageTypes"), TEXT(""))
			.Number(TEXT("quadsPerSection"), TEXT(""))
			.Number(TEXT("count"), TEXT(""))
			.Array(TEXT("assets"), TEXT(""))
			.Number(TEXT("numLODs"), TEXT(""))
			.Number(TEXT("subdivisions"), TEXT(""))
			.Number(TEXT("tileSize"), TEXT(""))
			.String(TEXT("quality"), TEXT(""))
			.String(TEXT("staticMesh"), TEXT("Mesh asset path."))
			.Number(TEXT("timeoutMs"), TEXT(""))
			.String(TEXT("path"), TEXT("Path to a directory."))
			.String(TEXT("filename"), TEXT(""))
			.String(TEXT("heightmapPath"), TEXT("Filesystem path to a heightmap file."))
			.String(TEXT("outputPath"), TEXT("Filesystem output path."))
			.String(TEXT("landscapePath"), TEXT("Landscape actor/object path."))
			.String(TEXT("landscapeActorPath"), TEXT("Landscape actor object path."))
			.String(TEXT("layerInfoPath"), TEXT("Landscape layer info asset path."))
			.String(TEXT("physicalMaterialPath"), TEXT("Physical material asset path."))
			.Bool(TEXT("noWeightBlend"), TEXT("Create a non-weight-blended landscape layer info."))
			.Number(TEXT("hardness"), TEXT("Landscape layer hardness."))
			.ArrayOfObjects(TEXT("layers"), TEXT("Landscape material layer-blend definitions."))
			.String(TEXT("blendType"), TEXT("Landscape layer blend mode."))
			.StringEnum(TEXT("maskType"), { TEXT("constant"), TEXT("height"), TEXT("slope"), TEXT("altitude"), TEXT("noise") }, TEXT("Landscape paint-rule mask source."))
			.Number(TEXT("targetHeight"), TEXT("Target height for flatten/ramp authoring."))
			.Number(TEXT("iterations"), TEXT("Procedural terrain or erosion iterations."))
			.Number(TEXT("frequency"), TEXT("Procedural terrain noise frequency."))
			.Number(TEXT("resolutionX"), TEXT("Requested landscape vertex resolution on X."))
			.Number(TEXT("resolutionY"), TEXT("Requested landscape vertex resolution on Y."))
			.StringEnum(TEXT("terrainFeature"), { TEXT("mountains"), TEXT("hills"), TEXT("valleys"), TEXT("plains"), TEXT("lakeshore"), TEXT("erosion") }, TEXT("Procedural landscape feature."))
			.StringEnum(TEXT("placementMode"), { TEXT("auto"), TEXT("landscape_grass"), TEXT("pcg"), TEXT("hism") }, TEXT("Scalable foliage placement backend."))
			.String(TEXT("foliageName"), TEXT("Tool-generated foliage collection name."))
			.ArrayOfObjects(TEXT("exclusionZones"), TEXT("World-space exclusion boxes with min/max vectors."))
			.Array(TEXT("excludedActors"), TEXT("Road, building, water, or gameplay-clearance actor labels."))
			.Number(TEXT("minSlope"), TEXT("Minimum allowed terrain slope in degrees."))
			.Number(TEXT("maxSlope"), TEXT("Maximum allowed terrain slope in degrees."))
			.Number(TEXT("minHeight"), TEXT("Minimum allowed world height."))
			.Number(TEXT("maxHeight"), TEXT("Maximum allowed world height."))
			.Number(TEXT("surfaceOffset"), TEXT("Vertical offset from traced landscape surface."))
			.Bool(TEXT("generatedOnly"), TEXT("Limit inspection/clear to MCP-generated foliage."))
			.Bool(TEXT("cancel"), TEXT("Cancel an unstarted landscape operation."))
			.Array(TEXT("assetPaths"), TEXT(""))
			.Array(TEXT("names"), TEXT(""))
			.Number(TEXT("time"), TEXT(""))
			.Number(TEXT("spacing"), TEXT(""))
			.Number(TEXT("heightScale"), TEXT(""))
			.String(TEXT("material"), TEXT("Material asset path."))
			.String(TEXT("particleSystemPath"), TEXT("Particle system asset path."))
			.String(TEXT("curvePath"), TEXT("Curve asset path or directory."))
			.FreeformObject(TEXT("settings"), TEXT("Properties to apply to created/configured environment objects."))
			.Number(TEXT("waveHeight"), TEXT("Water wave height."))
			.Number(TEXT("waveLength"), TEXT("Water wave length."))
			.Number(TEXT("amplitude"), TEXT("Wave or effect amplitude."))
			.Number(TEXT("steepness"), TEXT("Water wave steepness, clamped from 0 to 1."))
			.Number(TEXT("speed"), TEXT("Speed value."))
			.Object(TEXT("direction"), TEXT("Direction or rotation value."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("pitch")).Number(TEXT("yaw")).Number(TEXT("roll"));
			})
			.Number(TEXT("hour"), TEXT(""))
			.Number(TEXT("intensity"), TEXT(""))
			.Number(TEXT("skyLightIntensity"), TEXT("Sky light intensity for time-of-day systems."))
			.Number(TEXT("azimuth"), TEXT("Sun azimuth."))
			.Number(TEXT("elevation"), TEXT("Sun elevation."))
			.Bool(TEXT("collisionEnabled"), TEXT("Enable collision on configured environment actors."))
			.Number(TEXT("materialIndex"), TEXT("Material slot index."))
			.StringEnum(TEXT("lightType"), {
				TEXT("Directional"),
				TEXT("Point"),
				TEXT("Spot"),
				TEXT("Rect"),
				TEXT("DirectionalLight"),
				TEXT("PointLight"),
				TEXT("SpotLight"),
				TEXT("RectLight"),
				TEXT("directional"),
				TEXT("point"),
				TEXT("spot"),
				TEXT("rect")
			}, TEXT("Light type."))
			.String(TEXT("lightClass"), TEXT("Unreal light class name."))
			.Array(TEXT("color"), TEXT("RGBA color as an array [r, g, b, a]."), TEXT("number"))
			.Bool(TEXT("castShadows"), TEXT("Whether the light casts shadows."))
			.Bool(TEXT("useAsAtmosphereSunLight"), TEXT("Use a Directional Light as the atmosphere sun light."))
			.Number(TEXT("temperature"), TEXT("Color temperature."))
			.Number(TEXT("falloffExponent"), TEXT("Light falloff exponent."))
			.Number(TEXT("innerCone"), TEXT("Spot light inner cone angle."))
			.Number(TEXT("outerCone"), TEXT("Spot light outer cone angle."))
			.Number(TEXT("width"), TEXT("Width value."))
			.Number(TEXT("height"), TEXT("Height value."))
			.StringEnum(TEXT("sourceType"), {
				TEXT("CapturedScene"),
				TEXT("SpecifiedCubemap")
			}, TEXT("Sky light source type."))
			.String(TEXT("cubemapPath"), TEXT("Texture asset path."))
			.Bool(TEXT("recapture"), TEXT("Whether to recapture sky lighting."))
			.StringEnum(TEXT("method"), {
				TEXT("Lightmass"),
				TEXT("LumenGI"),
				TEXT("ScreenSpace"),
				TEXT("None")
			}, TEXT("Lighting method."))
			.Number(TEXT("indirectLightingIntensity"), TEXT("Indirect lighting intensity."))
			.Number(TEXT("bounces"), TEXT("Light bounce count."))
			.String(TEXT("shadowQuality"), TEXT("Shadow quality setting."))
			.Bool(TEXT("cascadedShadows"), TEXT("Whether cascaded shadows are enabled."))
			.Number(TEXT("shadowDistance"), TEXT("Shadow distance."))
			.Bool(TEXT("contactShadows"), TEXT("Whether contact shadows are enabled."))
			.Bool(TEXT("rayTracedShadows"), TEXT("Whether ray-traced shadows are enabled."))
			.String(TEXT("lightName"), TEXT("Light actor name."))
			.String(TEXT("lightPath"), TEXT("Light actor object path."))
			.Number(TEXT("channel"), TEXT("Lighting channel index from 0 to 2."))
			.FreeformObject(TEXT("channels"), TEXT("Lighting channel state with channel0, channel1, and channel2 booleans."))
			.Bool(TEXT("applyToAllComponents"), TEXT("Apply actor lighting channels to all primitive components."))
			.Number(TEXT("compensationValue"), TEXT("Exposure compensation value."))
			.Number(TEXT("minBrightness"), TEXT("Minimum brightness."))
			.Number(TEXT("maxBrightness"), TEXT("Maximum brightness."))
			.Bool(TEXT("enabled"), TEXT("Whether the item/feature is enabled."))
			.Number(TEXT("scatteringIntensity"), TEXT("Fog scattering intensity."))
			.Number(TEXT("fogHeight"), TEXT("Fog height."))
			.Bool(TEXT("buildOnlySelected"), TEXT("Build lighting only for selected actors."))
			.Bool(TEXT("buildReflectionCaptures"), TEXT("Build reflection captures."))
			.String(TEXT("levelName"), TEXT("Level name."))
			.Bool(TEXT("copyActors"), TEXT("Copy actors into a created lighting level."))
			.Bool(TEXT("useTemplate"), TEXT("Use a template when creating a level."))
			.Object(TEXT("size"), TEXT("3D scale (x, y, z)."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.String(TEXT("actorPath"), TEXT("Path to actor."))
			.String(TEXT("splineName"), TEXT("Name of spline component."))
			.String(TEXT("componentName"), TEXT("Name of the component."))
			.String(TEXT("blueprintPath"), TEXT("Blueprint asset path."))
			.Object(TEXT("scale"), TEXT("3D scale (x, y, z)."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.Number(TEXT("pointIndex"), TEXT("Index of spline point to modify."))
			.Object(TEXT("arriveTangent"), TEXT("Arrive tangent for spline point."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.Object(TEXT("leaveTangent"), TEXT("Leave tangent for spline point."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.Object(TEXT("tangent"), TEXT("Unified spline tangent."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.Object(TEXT("pointRotation"), TEXT("Rotation at spline point."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("pitch")).Number(TEXT("yaw")).Number(TEXT("roll"));
			})
			.Object(TEXT("pointScale"), TEXT("Scale at spline point."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.StringEnum(TEXT("coordinateSpace"), {
				TEXT("Local"),
				TEXT("World")
			}, TEXT("Coordinate space for spline values."))
			.StringEnum(TEXT("splineType"), {
				TEXT("Linear"),
				TEXT("Curve"),
				TEXT("Constant"),
				TEXT("CurveClamped"),
				TEXT("CurveCustomTangent")
			}, TEXT("Type of spline interpolation."))
			.Bool(TEXT("bClosedLoop"), TEXT("Whether spline forms a closed loop."))
			.Bool(TEXT("bUpdateSpline"), TEXT("Update spline after modification."))
			.StringEnum(TEXT("forwardAxis"), {
				TEXT("X"),
				TEXT("Y"),
				TEXT("Z")
			}, TEXT("Forward axis for spline mesh deformation."))
			.Object(TEXT("startPos"), TEXT("Start position for spline mesh segment."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.Object(TEXT("startTangent"), TEXT("Start tangent for spline mesh segment."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.Object(TEXT("endPos"), TEXT("End position for spline mesh segment."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.Object(TEXT("endTangent"), TEXT("End tangent for spline mesh segment."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y")).Number(TEXT("z"));
			})
			.Object(TEXT("startScale"), TEXT("X/Y scale at spline mesh start."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y"));
			})
			.Object(TEXT("endScale"), TEXT("X/Y scale at spline mesh end."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("x")).Number(TEXT("y"));
			})
			.Number(TEXT("startRoll"), TEXT("Roll angle at spline mesh start."))
			.Number(TEXT("endRoll"), TEXT("Roll angle at spline mesh end."))
			.Bool(TEXT("bSmoothInterpRollScale"), TEXT("Use smooth interpolation for roll/scale."))
			.Number(TEXT("startOffset"), TEXT("Offset from spline start for first mesh."))
			.Number(TEXT("endOffset"), TEXT("Offset from spline end for last mesh."))
			.Bool(TEXT("bAlignToSpline"), TEXT("Align scattered meshes to spline direction."))
			.Bool(TEXT("bRandomizeRotation"), TEXT("Apply random rotation to scattered meshes."))
			.Object(TEXT("rotationRandomRange"), TEXT("Random rotation range (degrees)."),
				[](FMcpSchemaBuilder& S) {
				S.Number(TEXT("pitch")).Number(TEXT("yaw")).Number(TEXT("roll"));
			})
			.Bool(TEXT("bRandomizeScale"), TEXT("Apply random scale to scattered meshes."))
			.Number(TEXT("scaleMin"), TEXT("Minimum random scale multiplier."))
			.Number(TEXT("scaleMax"), TEXT("Maximum random scale multiplier."))
			.Number(TEXT("randomSeed"), TEXT("Seed for randomization."))
			.StringEnum(TEXT("templateType"), {
				TEXT("road"),
				TEXT("river"),
				TEXT("fence"),
				TEXT("wall"),
				TEXT("cable"),
				TEXT("pipe")
			}, TEXT("Type of spline template to create."))
			.Number(TEXT("segmentLength"), TEXT("Length of mesh segments for deformation."))
			.Number(TEXT("postSpacing"), TEXT("Spacing between fence posts."))
			.Number(TEXT("railHeight"), TEXT("Height of fence rails."))
			.Number(TEXT("pipeRadius"), TEXT("Radius for pipe template."))
			.Number(TEXT("cableSlack"), TEXT("Slack/sag amount for cable template."))
			.ArrayOfObjects(TEXT("points"), TEXT("Spline points."))
			.ArrayOfObjects(TEXT("routePoints"), TEXT("Ordered world- or local-space spline route points."))
			.Number(TEXT("index"), TEXT("Spline point insertion index."))
			.Number(TEXT("roll"), TEXT("Spline point roll in degrees."))
			.Bool(TEXT("collisionEnabled"), TEXT("Enable collision on generated spline meshes."))
			.String(TEXT("filter"), TEXT("General search filter."))
			.Bool(TEXT("save"), TEXT("Save the asset(s) after the operation."))
			.Required({TEXT("action")})
			.Build();
	}
};

MCP_REGISTER_TOOL(FMcpTool_BuildEnvironment);
