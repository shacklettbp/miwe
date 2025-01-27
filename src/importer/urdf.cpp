#include "urdf.hpp"
#include <madrona/math.hpp>
#include <madrona/cvphysics.hpp>

#include <map>
#include <string>
#include <sstream>
#include <fstream>
#include <tinyxml2.h>

#define massert(exp, msg) assert((void(msg), exp))

namespace madrona::imp {

using namespace math;

struct URDFMaterial {
    std::string name;
    std::string texturePath;
    Vector4 color;
};

struct URDFPose {
    Vector3 position;
    Quat rotation;

    static URDFPose identity()
    {
        return {
            { 0.f, 0.f, 0.f },
            Quat::id(),
        };
    }
};

struct URDFInertial {
    URDFPose origin;
    float mass;
    float ixx, ixy, ixz, iyy, iyz, izz;
};

struct URDFSphere {
    float radius;
};

struct URDFBox {
    Vector3 dim;
};

struct URDFCylinder {
    float length;
    float radius;
};

struct URDFMesh {
    std::string filename;
    Vector3 scale;
};

enum class URDFGeometryType {
    Sphere,
    Box,
    Cylinder,
    Mesh
};

struct URDFGeometry {
    URDFGeometryType type;
    URDFSphere sphere;
    URDFBox box;
    URDFCylinder cylinder;
    URDFMesh mesh;
};

struct URDFVisual {
    std::string name;
    URDFPose origin;
    URDFGeometry geometry;
    std::string materialName;
    URDFMaterial material;
};

struct URDFCollision {
    std::string name;
    URDFPose origin;
    URDFGeometry geometry;
};

struct URDFLink {
    std::string name;
    URDFInertial inertial;
    std::vector<URDFVisual> visualArray;
    std::vector<URDFCollision> collisionArray;

    std::string parentLinkName;
    std::string parentJointName;

    std::vector<std::string> childJointNames;
    std::vector<std::string> childLinkNames;
};

enum class URDFJointType {
    Revolute, // Hinge with limits
    Continuous, // Hinge without limits
    Prismatic, // Sliding joint with limits
    Floating,
    Planar,
    Fixed,
    Invalid,
};

struct URDFJointDynamics {
    float damping;
    float friction;
};

struct URDFJointLimits {
    float lower;
    float upper;
    float effort;
    float velocity;
};

struct URDFJointSafety {
    float softUpperLimit;
    float softLowerLimit;
    float kPosition;
    float kVelocity;
};

struct URDFJointCalibration {
    float referencePosition;
    float rising;
    float falling;
};

struct URDFJointMimic {
    float offset;
    float multiplier;
    std::string jointName;
};

struct URDFJoint {
    std::string name;
    URDFJointType type;
    Vector3 axis;
    std::string childLinkName;
    std::string parentLinkName;
    URDFPose parentToJointOriginTransform;
    URDFJointDynamics dynamics;
    URDFJointLimits limits;
    URDFJointSafety safety;
    URDFJointCalibration calibration;
    URDFJointMimic mimic;
};

struct URDFModel {
    std::string name;
    std::map<std::string, URDFMaterial> materials;
    std::map<std::string, URDFLink> links;
    std::map<std::string, URDFJoint> joints;
    std::string rootLinkName;
};

struct URDFLoader::Impl {
    
};

static std::vector<float> getFloats(const std::string &vector_str)
{
    std::vector<float> values;

    std::vector<std::string> pieces;

    { // Split the string by spaces
        std::string temp;
        std::stringstream s(vector_str);
        while(s >> temp)
            pieces.push_back(temp);
    }

    for (unsigned int i = 0; i < pieces.size(); ++i) {
        if (!pieces[i].empty()) {
            double piece = std::stof(pieces[i]);
            values.push_back(static_cast<float>(piece));
        }
    }

    return values;
}

static Vector4 getVector4(const std::string &vector_str)
{
    auto values = getFloats(vector_str);

    massert(values.size() == 4, "URDF Loading: Vector4 doesn't have 4 floats");

    return {
        values[0], values[1], values[2], values[3],
    };
}

static Vector3 getVector3(const std::string &vector_str)
{
    auto values = getFloats(vector_str);

    massert(values.size() == 3, "URDF Loading: Vector3 doesn't have 3 floats");

    return {
        values[0], values[1], values[2]
    };
}

static Quat getQuatFromRPY(const std::string &vector_str)
{
    auto values = getFloats(vector_str);

    massert(values.size() == 3, "URDF Loading: RPY doesn't have 3 floats");

    Vector3 rpy = { values[0], values[1], values[2] };

    float phi = rpy.x / 2.0;
    float the = rpy.y / 2.0;
    float psi = rpy.z / 2.0;

    Quat ret = {};

    ret.x = sin(phi) * cos(the) * cos(psi) - cos(phi) * sin(the) * sin(psi);
    ret.y = cos(phi) * sin(the) * cos(psi) + sin(phi) * cos(the) * sin(psi);
    ret.z = cos(phi) * cos(the) * sin(psi) - sin(phi) * sin(the) * cos(psi);
    ret.w = cos(phi) * cos(the) * cos(psi) + sin(phi) * sin(the) * sin(psi);

    ret = ret.normalize();

    return ret;
}

static void parsePoseInternal(
    URDFPose &pose,
    tinyxml2::XMLElement* xml)
{
    if (xml) {
        const char* xyz_str = xml->Attribute("xyz");

        if (xyz_str != NULL) {
            pose.position = getVector3(xyz_str);
        }

        const char* rpy_str = xml->Attribute("rpy");
        if (rpy_str != NULL) {
            pose.rotation = getQuatFromRPY(rpy_str);
        }
    }
}

static void parseSphere(URDFSphere &s, tinyxml2::XMLElement *c)
{
    massert(c->Attribute("radius"), "Sphere doesn't have radius");

    s.radius = std::stof(c->Attribute("radius"));
}

static void parseBox(URDFBox &b, tinyxml2::XMLElement *c)
{
    massert(c->Attribute("size"), "Box shape has no size attribute");

    b.dim = getVector3(c->Attribute("size"));
}

static void parseCylinder(URDFCylinder &y, tinyxml2::XMLElement *c)
{
    massert(c->Attribute("length") && c->Attribute("radius"),
            "Cylinder shape must have both length and radius attributes");

    y.length = std::stof(c->Attribute("length"));
    y.radius = std::stof(c->Attribute("radius"));
}

static void parseMesh(URDFMesh &m, tinyxml2::XMLElement *c)
{
    massert(c->Attribute("filename"), "Mesh must contain a filename attribute");
    
    m.filename = c->Attribute("filename");

    if (c->Attribute("scale")) {
        m.scale = getVector3(c->Attribute("scale"));
    } else {
        m.scale.x = m.scale.y = m.scale.z = 1;
    }
}

static void parseGeometry(URDFGeometry &geom, tinyxml2::XMLElement *g)
{
    massert(g, "URDF Loading: Geometry element doesn't exist");

    tinyxml2::XMLElement *shape = g->FirstChildElement();
    massert(shape, "URDF Loading: no shape");

    std::string type_name = shape->Value();
    if (type_name == "sphere") {
        geom.type = URDFGeometryType::Sphere;
        parseSphere(geom.sphere, shape);
    } else if (type_name == "box") {
        geom.type = URDFGeometryType::Box;
        parseBox(geom.box, shape);
    } else if (type_name == "cylinder") {
        geom.type = URDFGeometryType::Cylinder;
        parseCylinder(geom.cylinder, shape);
    } else if (type_name == "mesh") {
        geom.type = URDFGeometryType::Mesh;
        parseMesh(geom.mesh, shape);
    } else {
        printf("Unknown geometry type '%s'", type_name.c_str());
    }
}

static bool parseMaterial(
    URDFMaterial &material,
    tinyxml2::XMLElement *config,
    bool only_name_is_ok)
{
    bool has_rgb = false;
    bool has_filename = false;

    if (!config->Attribute("name")) {
        printf("Material must contain a name attribute\n");
        return false;
    }

    material.name = config->Attribute("name");

    // texture
    tinyxml2::XMLElement *t = config->FirstChildElement("texture");
    if (t) {
        if (t->Attribute("filename")) {
            material.texturePath = t->Attribute("filename");
            has_filename = true;
        }
    }

    // color
    tinyxml2::XMLElement *c = config->FirstChildElement("color");
    if (c) {
        if (c->Attribute("rgba")) {
            material.color = getVector4(c->Attribute("rgba"));
            has_rgb = true;
        }
    }

    if (!has_rgb && !has_filename) {
        if (!only_name_is_ok) { // no need for an error if only name is ok
            printf("Material [%s] color has no rgba\n", material.name.c_str());
            printf("Material [%s] not defined in file\n", material.name.c_str());
        }

        return false;
    }
    return true;
}

static bool parseVisual(URDFVisual &vis, tinyxml2::XMLElement *config)
{
    // Origin
    tinyxml2::XMLElement *o = config->FirstChildElement("origin");
    
    if (o) {
        parsePoseInternal(vis.origin, o);
    }

    // Geometry
    tinyxml2::XMLElement *geom = config->FirstChildElement("geometry");
    parseGeometry(vis.geometry, geom);

    const char *name_char = config->Attribute("name");
    if (name_char) {
        vis.name = name_char;
    }

    // Material
    tinyxml2::XMLElement *mat = config->FirstChildElement("material");
    if (mat) {
        // get material name
        massert(mat->Attribute("name"), "Visual material must contain a name attribute");

        vis.materialName = mat->Attribute("name");

        // try to parse material element in place
        parseMaterial(vis.material, mat, true);
    }
}

static void parseInertial(
        URDFInertial &i,
        tinyxml2::XMLElement *config)
{
    // Origin
    tinyxml2::XMLElement *o = config->FirstChildElement("origin");
    if (o) {
        parsePoseInternal(i.origin, o);
    }

    tinyxml2::XMLElement *mass_xml = config->FirstChildElement("mass");

    massert(mass_xml, "Inertial element must have a mass element");
    massert(mass_xml->Attribute("value"), "Inertial: mass element must have value");

    i.mass = std::stof(mass_xml->Attribute("value"));

    tinyxml2::XMLElement *inertia_xml = config->FirstChildElement("inertia");
    massert(inertia_xml, "Inertial element must have inertia element");

    std::vector<std::pair<std::string, double>> attrs{
        std::make_pair("ixx", 0.0),
        std::make_pair("ixy", 0.0),
        std::make_pair("ixz", 0.0),
        std::make_pair("iyy", 0.0),
        std::make_pair("iyz", 0.0),
        std::make_pair("izz", 0.0)
    };

    for (auto& attr : attrs) {
        massert(inertia_xml->Attribute(attr.first.c_str()),
                "Inertial: inertia element missing");

        attr.second = std::stof(inertia_xml->Attribute(attr.first.c_str()));
    }

    i.ixx = attrs[0].second;
    i.ixy = attrs[1].second;
    i.ixz = attrs[2].second;
    i.iyy = attrs[3].second;
    i.iyz = attrs[4].second;
    i.izz = attrs[5].second;
}

static void parseCollision(
        URDFCollision &col,
        tinyxml2::XMLElement *config)
{
    // Origin
    tinyxml2::XMLElement *o = config->FirstChildElement("origin");

    if (o) {
        parsePoseInternal(col.origin, o);
    }

    // Geometry
    tinyxml2::XMLElement *geom = config->FirstChildElement("geometry");
    parseGeometry(col.geometry, geom);

    const char *name_char = config->Attribute("name");

    if (name_char) {
        col.name = name_char;
    }
}

static bool parseLink(
        URDFLink &link,
        tinyxml2::XMLElement* config)
{
    const char *name_char = config->Attribute("name");

    massert(name_char, "URDF Loading: link doesn't have name");

    link.name = std::string(name_char);

    // Inertial (optional)
    tinyxml2::XMLElement *i = config->FirstChildElement("inertial");
    if (i) {
        parseInertial(link.inertial, i);
    }

    // Multiple Visuals (optional)
    for (tinyxml2::XMLElement* vis_xml = config->FirstChildElement("visual");
            vis_xml;
            vis_xml = vis_xml->NextSiblingElement("visual")) {
        URDFVisual vis;
        parseVisual(vis, vis_xml);
        link.visualArray.push_back(vis);
    }

    // Multiple Collisions (optional)
    for (tinyxml2::XMLElement* col_xml = config->FirstChildElement("collision");
            col_xml;
            col_xml = col_xml->NextSiblingElement("collision")) {
        URDFCollision col;
        parseCollision(col, col_xml);
        link.collisionArray.push_back(col);
    }
}

static void assignMaterial(
        URDFVisual& visual,
        URDFModel& model,
        const char* link_name)
{
    massert(!visual.materialName.empty(), "Assigning material with no name");

    const URDFMaterial &material = model.materials[visual.materialName];

    if (model.materials.contains(visual.materialName)) {
        visual.material = material;
    } else {
        model.materials.insert(std::make_pair(visual.material.name, visual.material));
    }
}

static void parseJointLimits(
        URDFJointLimits &jl,
        tinyxml2::XMLElement* config)
{
    // Get lower joint limit
    const char* lower_str = config->Attribute("lower");
    if (lower_str == NULL){
        printf("no lower, defaults to 0");
        jl.lower = 0;
    }
    else {
        jl.lower = std::stof(lower_str);
    }

    // Get upper joint limit
    const char* upper_str = config->Attribute("upper");
    if (upper_str == NULL){
        printf("urdfdom.joint_limit: no upper, , defaults to 0");
        jl.upper = 0;
    }
    else {
        jl.upper = std::stof(upper_str);
    }

    // Get joint effort limit
    const char* effort_str = config->Attribute("effort");
    if (effort_str == NULL){
        printf("joint limit: no effort");
    }
    else {
        jl.effort = std::stof(effort_str);
    }

    // Get joint velocity limit
    const char* velocity_str = config->Attribute("velocity");
    if (velocity_str == NULL) {
        printf("joint limit: no velocity");
    }
    else {
        jl.velocity = std::stof(velocity_str);
    }
}

static void parseJointSafety(
        URDFJointSafety &js,
        tinyxml2::XMLElement* config)
{
    // Get soft_lower_limit joint limit
    const char* soft_lower_limit_str = config->Attribute("soft_lower_limit");
    if (soft_lower_limit_str == NULL) {
        printf("urdfdom.joint_safety: no soft_lower_limit, using default value\n");
        js.softLowerLimit = 0;
    } else {
        js.softLowerLimit = std::stof(soft_lower_limit_str);
    }

    // Get soft_upper_limit joint limit
    const char* soft_upper_limit_str = config->Attribute("soft_upper_limit");
    if (soft_upper_limit_str == NULL) {
        printf("urdfdom.joint_safety: no soft_upper_limit, using default value\n");
        js.softUpperLimit = 0;
    } else {
        js.softUpperLimit = std::stof(soft_upper_limit_str);
    }

    // Get k_position_ safety "position" gain - not exactly position gain
    const char* k_position_str = config->Attribute("k_position");
    if (k_position_str == NULL) {
        printf("no k_position, using default value\n");
        js.kPosition = 0;
    } else {
        js.kPosition = std::stof(k_position_str);
    }
    // Get k_velocity_ safety velocity gain
    const char* k_velocity_str = config->Attribute("k_velocity");
    if (k_velocity_str == NULL) {
        printf("joint safety: no k_velocity\n");
    } else {
        js.kVelocity = std::stof(k_velocity_str);
    }
}

static void parseJointCalibration(
        URDFJointCalibration &jc,
        tinyxml2::XMLElement* config)
{
    // Get rising edge position
    const char* rising_position_str = config->Attribute("rising");
    if (rising_position_str == NULL) {
        printf("no rising, using default value\n");
        jc.referencePosition = 0.f;
    } else {
        jc.rising = std::stof(rising_position_str);
    }

    // Get falling edge position
    const char* falling_position_str = config->Attribute("falling");
    if (falling_position_str == NULL) {
        printf("urdfdom.joint_calibration: no falling, using default value");
        jc.falling = 0.f;
    } else {
        jc.falling = std::stof(falling_position_str);
    }
}

static void parseJointMimic(
        URDFJointMimic &jm,
        tinyxml2::XMLElement* config)
{
    // Get name of joint to mimic
    const char* joint_name_str = config->Attribute("joint");

    if (joint_name_str == NULL) {
        printf("joint mimic: no mimic joint specified\n");
        return;
    } else {
        jm.jointName = joint_name_str;
    }

    // Get mimic multiplier
    const char* multiplier_str = config->Attribute("multiplier");

    if (multiplier_str == NULL) {
        printf("urdfdom.joint_mimic: no multiplier, using default value of 1\n");
        jm.multiplier = 1;
    } else {
        jm.multiplier = std::stof(multiplier_str);
    }


    // Get mimic offset
    const char* offset_str = config->Attribute("offset");
    if (offset_str == NULL) {
        printf("urdfdom.joint_mimic: no offset, using default value of 0\n");
        jm.offset = 0.f;
    } else {
        jm.offset = std::stof(offset_str);
    }
}

static void parseJointDynamics(
        URDFJointDynamics &jd,
        tinyxml2::XMLElement* config)
{
    jd.damping = 0.f;
    jd.friction = 0.f;

    // Get joint damping
    const char* damping_str = config->Attribute("damping");
    if (damping_str == NULL){
        printf("urdfdom.joint_dynamics: no damping, defaults to 0\n");
        jd.damping = 0.f;
    } else {
        jd.damping = std::stof(damping_str);
    }

    // Get joint friction
    const char* friction_str = config->Attribute("friction");
    if (friction_str == NULL){
        printf("urdfdom.joint_dynamics: no friction, defaults to 0\n");
        jd.friction = 0;
    } else {
        jd.friction = std::stof(friction_str);
    }

    if (damping_str == NULL && friction_str == NULL) {
        printf("joint dynamics element specified with no damping and no friction\n");
    } else {
        printf("urdfdom.joint_dynamics: damping %f and friction %f", jd.damping, jd.friction);
    }
}

static void parseJoint(
        URDFJoint &joint,
        tinyxml2::XMLElement* config)
{
    // Get Joint Name
    const char *name = config->Attribute("name");

    massert(name, "Joint doesn't have a name");

    joint.name = name;

    // Get transform from Parent Link to Joint Frame
    tinyxml2::XMLElement *origin_xml = config->FirstChildElement("origin");
    if (!origin_xml) {
        printf("Joint doesn't have transform - setting to identity\n");
        joint.parentToJointOriginTransform = URDFPose::identity();
    } else {
        parsePoseInternal(joint.parentToJointOriginTransform, origin_xml);
    }

    // Get Parent Link
    tinyxml2::XMLElement *parent_xml = config->FirstChildElement("parent");
    if (parent_xml) {
        const char *pname = parent_xml->Attribute("link");
        if (!pname) {
            printf("no parent link name specified for Joint link [%s]. this might be the root?", 
                    joint.name.c_str());
        } else {
            joint.parentLinkName = std::string(pname);
        }
    }

    // Get Child Link
    tinyxml2::XMLElement *child_xml = config->FirstChildElement("child");
    if (child_xml) {
        const char *pname = child_xml->Attribute("link");
        if (!pname) {
            printf("no child link name specified for Joint link [%s].", joint.name.c_str());
        } else {
            joint.childLinkName = std::string(pname);
        }
    }

    // Get Joint type
    const char* type_char = config->Attribute("type");
    massert(type_char, "Joint doesn't have a type");

    std::string type_str = type_char;
    if (type_str == "planar")
        joint.type = URDFJointType::Planar;
    else if (type_str == "floating")
        joint.type = URDFJointType::Floating;
    else if (type_str == "revolute")
        joint.type = URDFJointType::Revolute;
    else if (type_str == "continuous")
        joint.type = URDFJointType::Continuous;
    else if (type_str == "prismatic")
        joint.type = URDFJointType::Prismatic;
    else if (type_str == "fixed")
        joint.type = URDFJointType::Fixed;
    else
        massert(false, "Joint doesn't have a known type");

    // Get Joint Axis
    if (joint.type != URDFJointType::Floating &&
            joint.type != URDFJointType::Fixed) {
        // axis
        tinyxml2::XMLElement *axis_xml = config->FirstChildElement("axis");
        if (!axis_xml) {
            printf("no axis element for Joint link [%s], defaulting to (1,0,0) axis", joint.name.c_str());
            joint.axis = Vector3(1.0, 0.0, 0.0);
        } else {
            if (axis_xml->Attribute("xyz")) {
                joint.axis = getVector3(axis_xml->Attribute("xyz"));
            }
        }
    }

    // Get limit
    tinyxml2::XMLElement *limit_xml = config->FirstChildElement("limit");
    if (limit_xml) {
        parseJointLimits(joint.limits, limit_xml);
    } else if (joint.type == URDFJointType::Revolute) {
        printf("Joint [%s] is of type REVOLUTE but it does not specify limits", joint.name.c_str());
    } else if (joint.type == URDFJointType::Prismatic) {
        printf("Joint [%s] is of type PRISMATIC without limits", joint.name.c_str());
    }

    // Get safety
    tinyxml2::XMLElement *safety_xml = config->FirstChildElement("safety_controller");
    if (safety_xml) {
        parseJointSafety(joint.safety, safety_xml);
    }

    // Get calibration
    tinyxml2::XMLElement *calibration_xml = config->FirstChildElement("calibration");
    if (calibration_xml) {
        parseJointCalibration(joint.calibration, calibration_xml);
    }

    // Get Joint Mimic
    tinyxml2::XMLElement *mimic_xml = config->FirstChildElement("mimic");
    if (mimic_xml) {
        parseJointMimic(joint.mimic, mimic_xml);
    }

    // Get Dynamics
    tinyxml2::XMLElement *prop_xml = config->FirstChildElement("dynamics");
    if (prop_xml) {
        parseJointDynamics(joint.dynamics, prop_xml);
    }
}

static void initTree(
        URDFModel &model,
        std::map<std::string, std::string> &parent_link_tree)
{
    // loop through all joints, for every link, assign children links and children joints
    for (auto joint = model.joints.begin();
            joint != model.joints.end();
            joint++) {
        std::string parent_link_name = joint->second.parentLinkName;
        std::string child_link_name = joint->second.childLinkName;
        
        massert(!parent_link_name.empty() && !child_link_name.empty(),
                "Joint is missing a parent and/or child link specification");

        // find child and parent links
        massert(model.links.contains(child_link_name),
                "child link not found");

        URDFLink &child_link = model.links[child_link_name];

        massert(model.links.contains(parent_link_name),
                "parent link not found");

        URDFLink &parent_link = model.links[parent_link_name];

        // Set parent link for child link
        child_link.parentLinkName = joint->second.parentLinkName;

        // Set parent joint
        child_link.parentJointName = joint->second.name;

        // Set child joint for parent link
        parent_link.childJointNames.push_back(joint->second.name);

        // Set child link for parent link
        parent_link.childLinkNames.push_back(child_link.name);

        // Fill in child/parent string map
        parent_link_tree[child_link.name] = parent_link_name;
    }
}

static void initRoot(
        URDFModel &model,
        const std::map<std::string, std::string> &parent_link_tree)
{ 
    // find the links that have no parent in the tree
    for (auto l = model.links.begin();
            l != model.links.end();
            l++) {
        std::map<std::string, std::string>::const_iterator parent =
            parent_link_tree.find(l->first);

        if (parent == parent_link_tree.end()) {
            if (model.rootLinkName == "") {
                model.rootLinkName = l->first;
            } else {
                massert(false, "Two root links found!");
            }
        }
    }

    massert(model.rootLinkName != "", "No root link found. "
            "The robot xml is not a valid tree");
}

static URDFModel parseURDF(const std::string &xml_string)
{
    URDFModel model;

    tinyxml2::XMLDocument xml_doc;
    xml_doc.Parse(xml_string.c_str());

    if (xml_doc.Error()) {
        printf("Failed to parse URDF xml: %s\n", xml_doc.ErrorStr());
        xml_doc.ClearError();
        assert(false);
    }

    tinyxml2::XMLElement *robot_xml = xml_doc.FirstChildElement("robot");
    if (!robot_xml) {
        printf("Could not find the 'robot' element in the xml file");
        assert(false);
    }

    // Get robot name
    const char *name = robot_xml->Attribute("name");
    if (!name) {
        printf("No name given for the robot.\n");
        assert(false);
    }
    model.name = std::string(name);

    // TODO: Check version is 1.0

    // Get all Material elements
    for (tinyxml2::XMLElement* material_xml = robot_xml->FirstChildElement("material");
            material_xml;
            material_xml = material_xml->NextSiblingElement("material")) {
        URDFMaterial material;

        bool success = parseMaterial(material, material_xml, false);
        assert(success);

        assert(!model.materials.contains(material.name));

        model.materials.insert(make_pair(material.name, material));
        // printf("successfully added a new material '%s'", material->name.c_str());
    }

    // Get all Link elements
    for (tinyxml2::XMLElement* link_xml = robot_xml->FirstChildElement("link");
            link_xml;
            link_xml = link_xml->NextSiblingElement("link")) {
        URDFLink link;

        parseLink(link, link_xml);

        massert(!model.links.contains(link.name), "link is not unique");

        if (!link.visualArray.empty()) {
            for (auto &visual : link.visualArray) {
                assignMaterial(visual, model, link.name.c_str());
            }

            model.links.insert(std::make_pair(link.name, link));
        }
    }

    massert(!model.links.empty(), "Model has no links");

    // Get all Joint elements
    for (tinyxml2::XMLElement* joint_xml = robot_xml->FirstChildElement("joint");
            joint_xml;
            joint_xml = joint_xml->NextSiblingElement("joint")) {
        URDFJoint joint;

        parseJoint(joint, joint_xml);

        massert(!model.joints.contains(joint.name),
                "Model joint not unique");

        model.joints.insert(std::make_pair(joint.name, joint));
    }

    // every link has children links and joints, but no parents, so we create a
    // local convenience data structure for keeping child->parent relations
    std::map<std::string, std::string> parent_link_tree;
    parent_link_tree.clear();

    // building tree: name mapping
    initTree(model, parent_link_tree);

    // find the root link
    initRoot(model, parent_link_tree);

    return model;
}

URDFLoader::URDFLoader(Span<char> err_buf)
{
}

URDFLoader::~URDFLoader()
{
}

phys::cv::ModelConfig convertToModelConfig(
        const URDFModel &model)
{
    // First, we need to create an ordering for the links
    std::vector<std::string> sorted_links;
    sorted_links.push_back(model.rootLinkName);
}

bool URDFLoader::load(
        const char *path,
        ImportedAssets &imported_assets)
{
    std::ifstream stream(path);

    if (!stream) {
        printf("File %s does not exist", path);
        return false;
    }

    std::string xml_str((std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());

    URDFModel model = parseURDF(xml_str);

    return true;
}

}
