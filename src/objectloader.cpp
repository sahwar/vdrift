#include "objectloader.h"
#include "texturemanager.h"
#include "textureinfo.h"
#include "modelmanager.h"
#include "tracksurface.h"
#include "trackobject.h"

#include <string>
#include <fstream>

static void operator >> (std::istream & lhs, std::vector<std::string> & rhs)
{
	for (size_t i = 0; i < rhs.size() && !lhs.eof(); ++i)
	{
		std::string str;
		std::getline(lhs, str, ',');
		std::stringstream s(str);
		s >> str;
		rhs[i] = str;
	}
}

/// TODO: weld together nearby geometry
void Optimize(const SCENENODE & input, SCENENODE & output)
{
	output = input;
}

bool OBJECTLOADER::BeginObjectLoad()
{
	list = true;
	packload = pack.LoadPack(objectpath + "/objects.jpk");
	
	std::string objectlist = objectpath + "/objects.txt";
	objectfile.open(objectlist.c_str());
	if (objectfile.good())
	{
		list = false;
		return Begin();
	}
	
	objectlist = objectpath + "/list.txt";
	objectfile.open(objectlist.c_str());
	if (!objectfile.good())
	{
		return false;
	}
	return BeginOld();
}

std::pair<bool, bool> OBJECTLOADER::ContinueObjectLoad()
{
	if (error)
	{
		return std::make_pair(true, false);
	}
	
	if (list)
	{
		return ContinueOld();
	}
	
	return Continue();
}

/*
[body.foo]
texture = diffuse.png
model = body.joe
#clampuv = 0
#mipmap = true
#skybox = false
#nolighting = false
#alphablend = false
#doublesided = false
#isashadow = false
#collideable = true
#surface = 0
#mass = 0 # to be implemented
#size = 1, 1, 1 # axis aligned bounding box

[node.bla]
body = foo
#position = 0, 0, 0
#rotation = 0, 0, 0
*/

OBJECTLOADER::body_iterator OBJECTLOADER::LoadBody(const std::string & name)
{
	body_iterator ib = bodies.find(name);
	if(ib != bodies.end())
	{
		return ib;
	}
	
	const PTree * sec = 0;
	if (!track_config.get("body." + name, sec))
	{
		std::string path = objectpath + "/" + name;
		std::ifstream file(path.c_str());
		if (!file)
		{
			return bodies.end();
		}
		
		PTree & bodysec = track_config.set("body." + name);
		read_ini(file, bodysec);
		sec = &bodysec;
	}
	
	BODY body;
	std::string texture_name;
	std::string model_name;
	int clampuv = 0;
	bool mipmap = true;
	bool skybox = false;
	bool alphablend = false;
	bool doublesided = false;
	bool isashadow = false;
	
	sec->get("texture", texture_name, error_output);
	sec->get("model", model_name, error_output);
	sec->get("clampuv", clampuv);
	sec->get("mipmap", mipmap);
	sec->get("skybox", skybox);
	sec->get("alphablend", alphablend);
	sec->get("doublesided", doublesided);
	sec->get("isashadow", isashadow);
	sec->get("nolighting", body.nolighting);
	sec->get("collidable", body.collidable);
	sec->get("surface", body.surface);
	
	if (dynamicshadowsenabled && isashadow)
	{
		return ib;
	}
	
	// load model
	if (packload)
	{
		if (!model_manager.Load(model_name, pack, body.model) &&
			!model_manager.Load(objectdir, model_name, body.model))
		{
			return ib;
		}
	}
	else
	{
		if (!model_manager.Load(objectdir, model_name, body.model))
		{
			return ib;
		}
	}
	models.push_back(body.model);
	
	// load textures
	std::vector<std::string> texture_names(3);
	std::stringstream s(texture_name);
	s >> texture_names;
	
	TEXTUREINFO texinfo;
	texinfo.mipmap = mipmap || anisotropy; //always mipmap if anisotropy is on
	texinfo.anisotropy = anisotropy;
	texinfo.repeatu = clampuv != 1 && clampuv != 2;
	texinfo.repeatv = clampuv != 1 && clampuv != 3;
	texinfo.size = texsize;
	
	std::tr1::shared_ptr<TEXTURE> diffuse;
	if (!texture_manager.Load(objectdir, texture_names[0], texinfo, diffuse))
	{
		return ib;
	}
	
	std::tr1::shared_ptr<TEXTURE> miscmap1;
	if (texture_names[1].length() > 0)
	{
		texture_manager.Load(objectdir, texture_names[1], texinfo, miscmap1);
	}
	
	std::tr1::shared_ptr<TEXTURE> miscmap2;
	if (texture_names[2].length() > 0)
	{
		texture_manager.Load(objectdir, texture_names[2], texinfo, miscmap2);
	}
	
	// setup drawable
	DRAWABLE & drawable = body.drawable;
	drawable.SetModel(*body.model);
	drawable.SetDiffuseMap(diffuse);
	drawable.SetMiscMap1(miscmap1);
	drawable.SetMiscMap2(miscmap2);
	drawable.SetDecal(alphablend);
	drawable.SetCull(cull && !doublesided, false);
	drawable.SetRadius(body.model->GetRadius());
	drawable.SetObjectCenter(body.model->GetCenter());
	drawable.SetSkybox(skybox);
	drawable.SetVerticalTrack(skybox && vertical_tracking_skyboxes);
	
	return bodies.insert(std::make_pair(name, body)).first;
}

void OBJECTLOADER::AddBody(SCENENODE & scene, const BODY & body)
{
	//use a different drawlist layer where necessary
	bool nolighting = body.nolighting;
	bool alphablend = body.drawable.GetDecal();
	bool skybox = body.drawable.GetSkybox();
	keyed_container<DRAWABLE> * dlist = &scene.GetDrawlist().normal_noblend;
	if (alphablend)
	{
		dlist = &scene.GetDrawlist().normal_blend;
	}
	else if (nolighting)
	{
		dlist = &scene.GetDrawlist().normal_noblend_nolighting;
	}
	if (skybox)
	{
		if (alphablend)
		{
			dlist = &scene.GetDrawlist().skybox_blend;
		}
		else
		{
			dlist = &scene.GetDrawlist().skybox_noblend;
		}
	}
	dlist->insert(body.drawable);
}

bool OBJECTLOADER::LoadNode(const PTree & sec)
{
	std::string bodyname;
	if (!sec.get("body", bodyname, error_output))
	{
		return false;
	}
	
	body_iterator ib = LoadBody(bodyname);
	if (ib != bodies.end())
	{
		MATHVECTOR<float, 3> pos;
		MATHVECTOR<float, 3> ang;
		QUATERNION<float> rot;
		const BODY & body = ib->second;
		if (sec.get("position",  pos) | sec.get("rotation", ang))
		{
			keyed_container <SCENENODE>::handle sh = unoptimized_scene.AddNode();
			SCENENODE & node = unoptimized_scene.GetNode(sh);
			
			rot.SetEulerZYX(ang[0]/180*M_PI, ang[1]/180*M_PI, ang[2]/180*M_PI);
			node.GetTransform().SetRotation(rot);
			node.GetTransform().SetTranslation(pos);
			
			AddBody(node, body);
		}
		else
		{
			AddBody(unoptimized_scene, body);
		}
		
		if (body.collidable)
		{
			assert(body.surface >= 0 && body.surface < (int)surfaces.size());
			
			TRACKOBJECT t;
			t.model = body.model.get();
			t.surface = &surfaces[body.surface];
			t.position = pos;
			t.rotation = rot;
			objects.push_back(t);
		}
	}
	
	return true;
}

bool OBJECTLOADER::Begin()
{
	std::string path = objectpath + "/objects.txt";
	std::ifstream file(path.c_str());
	if (file)
	{
		read_ini(file, track_config);
		//write_ini(track_config, std::cerr);
		nodes = 0;
		if (track_config.get("node", nodes))
		{
			node_it = nodes->begin();
			numobjects = nodes->size();
			models.reserve(numobjects);
			objects.reserve(numobjects);
			return true;
		}
	}
	return false;
}

std::pair<bool, bool> OBJECTLOADER::Continue()
{
	if (node_it == nodes->end())
	{
		// we're done, put the optimized scene in sceneroot
		Optimize(unoptimized_scene, sceneroot);
		unoptimized_scene.Clear();
		track_config.clear();
		bodies.clear();
		return std::make_pair(false, false);
	}
	
	if (!LoadNode(node_it->second))
	{
		return std::make_pair(true, false);
	}
	
	node_it++;
	
	return std::make_pair(false, true);
}

/// read from the file stream and put it in "output".
/// return true if the get was successful, else false
template <typename T>
static bool GetParam(std::ifstream & f, T & output)
{
	if (!f.good()) return false;

	std::string instr;
	f >> instr;
	if (instr.empty()) return false;

	while (!instr.empty() && instr[0] == '#' && f.good())
	{
		f.ignore(1024, '\n');
		f >> instr;
	}

	if (!f.good() && !instr.empty() && instr[0] == '#') return false;

	std::stringstream sstr(instr);
	sstr >> output;
	return true;
}

void OBJECTLOADER::CalculateNumOld()
{
	numobjects = 0;
	std::string objectlist = objectpath + "/list.txt";
	std::ifstream f(objectlist.c_str());
	int params_per_object;
	if (GetParam(f, params_per_object))
	{
		std::string junk;
		while (GetParam(f, junk))
		{
			for (int i = 0; i < params_per_object-1; ++i)
			{
				GetParam(f, junk);
			}
			numobjects++;
		}
	}
}

bool OBJECTLOADER::BeginOld()
{
	CalculateNumOld();
	
	models.reserve(numobjects);
	objects.reserve(numobjects);
	
	if (!GetParam(objectfile, params_per_object)) return false;
	
	if (params_per_object != expected_params)
	{
		info_output << "Track object list has " << params_per_object << " params per object, expected " << expected_params << ", this is fine, continuing" << std::endl;
	}
	
	if (params_per_object < min_params)
	{
		error_output << "Track object list has " << params_per_object << " params per object, expected " << expected_params << std::endl;
		return false;
	}
	
	return true;
}

std::pair<bool, bool> OBJECTLOADER::ContinueOld()
{
	std::string model_name;
	if (!(GetParam(objectfile, model_name)))
	{
		// we're done
		Optimize(unoptimized_scene, sceneroot); // put the optimized scene in sceneroot
		unoptimized_scene.Clear();
		return std::make_pair(false, false);
	}
	
	assert(objectfile.good());

	std::string diffuse_texture_name;
	bool mipmap;
	bool nolighting;
	bool skybox;
	int transparent_blend;
	float bump_wavelength;
	float bump_amplitude;
	bool driveable;
	bool collideable;
	float friction_notread;
	float friction_tread;
	float rolling_resistance;
	float rolling_drag;
	bool isashadow(false);
	int clamptexture(0);
	int surface_id(0);
	std::string otherjunk;

	GetParam(objectfile, diffuse_texture_name);
	GetParam(objectfile, mipmap);
	GetParam(objectfile, nolighting);
	GetParam(objectfile, skybox);
	GetParam(objectfile, transparent_blend);
	GetParam(objectfile, bump_wavelength);
	GetParam(objectfile, bump_amplitude);
	GetParam(objectfile, driveable);
	GetParam(objectfile, collideable);
	GetParam(objectfile, friction_notread);
	GetParam(objectfile, friction_tread);
	GetParam(objectfile, rolling_resistance);
	GetParam(objectfile, rolling_drag);

	if (params_per_object >= 15)
		GetParam(objectfile, isashadow);

	if (params_per_object >= 16)
		GetParam(objectfile, clamptexture);

	if (params_per_object >= 17)
		GetParam(objectfile, surface_id);

	for (int i = 0; i < params_per_object - expected_params; i++)
		GetParam(objectfile, otherjunk);

	if (dynamicshadowsenabled && isashadow)
	{
		return std::make_pair(false, true);
	}

	std::tr1::shared_ptr<MODEL> model;
	if (packload)
	{
		if (!model_manager.Load(model_name, pack, model))
		{
			return std::make_pair(true, false);
		}
	}
	else
	{
		if (!model_manager.Load(objectdir, model_name, model))
		{
			return std::make_pair(true, false);
		}
	}
	models.push_back(model);

	TEXTUREINFO texinfo;
	texinfo.mipmap = mipmap || anisotropy; //always mipmap if anisotropy is on
	texinfo.anisotropy = anisotropy;
	texinfo.repeatu = clamptexture != 1 && clamptexture != 2;
	texinfo.repeatv = clamptexture != 1 && clamptexture != 3;
	texinfo.size = texsize;

	std::tr1::shared_ptr<TEXTURE> diffuse_texture;
	if (!texture_manager.Load(objectdir, diffuse_texture_name, texinfo, diffuse_texture))
	{
		error_output << "Skipping object " << model_name << " and continuing" << std::endl;
		return std::make_pair(false, true);
	}

	std::tr1::shared_ptr<TEXTURE> miscmap1_texture;		
	{
		std::string texture_name = diffuse_texture_name.substr(0, std::max(0, (int)diffuse_texture_name.length()-4)) + "-misc1.png";
		std::string filepath = objectpath + "/" + texture_name;
		if (std::ifstream(filepath.c_str()))
		{
			if (!texture_manager.Load(objectdir, texture_name, texinfo, miscmap1_texture))
			{
				error_output << "Error loading texture: " << filepath << " for object " << model_name << ", continuing" << std::endl;
			}
		}
	}

	std::tr1::shared_ptr<TEXTURE> miscmap2_texture;
	{
		std::string texture_name = diffuse_texture_name.substr(0, std::max(0, (int)diffuse_texture_name.length()-4)) + "-misc2.png";
		std::string filepath = objectpath + "/" + texture_name;
		if (std::ifstream(filepath.c_str()))
		{
			if (!texture_manager.Load(objectdir, texture_name, texinfo, miscmap2_texture))
			{
				error_output << "Error loading texture: " << filepath << " for object " << model_name << ", continuing" << std::endl;
			}
		}
	}

	//use a different drawlist layer where necessary
	bool transparent = (transparent_blend==1);
	keyed_container <DRAWABLE> * dlist = &unoptimized_scene.GetDrawlist().normal_noblend;
	if (transparent)
	{
		dlist = &unoptimized_scene.GetDrawlist().normal_blend;
	}
	else if (nolighting)
	{
		dlist = &unoptimized_scene.GetDrawlist().normal_noblend_nolighting;
	}
	if (skybox)
	{
		if (transparent)
		{
			dlist = &unoptimized_scene.GetDrawlist().skybox_blend;
		}
		else
		{
			dlist = &unoptimized_scene.GetDrawlist().skybox_noblend;
		}
	}
	keyed_container <DRAWABLE>::handle dref = dlist->insert(DRAWABLE());
	DRAWABLE & drawable = dlist->get(dref);
	drawable.SetModel(*model);
	drawable.SetDiffuseMap(diffuse_texture);
	drawable.SetMiscMap1(miscmap1_texture);
	drawable.SetMiscMap2(miscmap2_texture);
	drawable.SetDecal(transparent);
	drawable.SetCull(cull && (transparent_blend!=2), false);
	drawable.SetRadius(model->GetRadius());
	drawable.SetObjectCenter(model->GetCenter());
	drawable.SetSkybox(skybox);
	drawable.SetVerticalTrack(skybox && vertical_tracking_skyboxes);

	if (collideable || driveable)
	{
		assert(surface_id >= 0 && surface_id < (int)surfaces.size());
		
		TRACKOBJECT t;
		t.model = model.get();
		t.surface = &surfaces[surface_id];
		objects.push_back(t);
	}

	return std::make_pair(false, true);
}
