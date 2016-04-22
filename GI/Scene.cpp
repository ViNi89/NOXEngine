#include <unordered_map>
#include <string>
#include <set>
#include <exception>
#include <iostream>

#include "Scene.h"

#include "GLUtils.h"
#include "Engine.h"

#include "Renderer.h"
#include "Entity.h"
#include "Job.h"
#include "Program.h"
#include "JobFactory.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>
#include <boost/log/trivial.hpp>
#include <boost/assign/list_of.hpp> // for 'map_list_of()'
#include <boost/multi_array.hpp>

#include "Scheduler.h"
#include "Camera.h"
#include "Light.h"
#include "Voxelizer.h"
#include "DistanceField.h"
#include "RayMarcher.h"
#include "ReflectiveShadowMap.h"
#include "ResourceManager.h"

#include "ShapeUtils.h"

#include <type_traits>

// FreeImage
#include <freeimage/FreeImage.h>

namespace pt = boost::property_tree;

nxScene::nxScene(nxEngine* eng) {
	m_pEngine = eng;
	m_Camera = NULL;

	m_GMaxX = m_GMaxY = m_GMaxZ = -100000.0f;
	m_GMinX = m_GMinY = m_GMinZ = 100000.0f;

}

nxScene::nxScene(std::string& path) {
	m_SceneFilename = path;
	m_Camera = NULL;
}

std::unordered_map<std::string, GLenum> gc_TypeMappings =
boost::assign::map_list_of("Compute", GL_COMPUTE_SHADER)
("Vertex", GL_VERTEX_SHADER)
("Tess_control", GL_TESS_CONTROL_SHADER)
("Tess_evaluation", GL_TESS_EVALUATION_SHADER)
("Geometry", GL_GEOMETRY_SHADER)
("Fragment", GL_FRAGMENT_SHADER);

static const float gridSpan = 300.0f;

void nxScene::Init() {
	try {
		// Create empty property tree object
		pt::ptree tree;
		BOOST_LOG_TRIVIAL(info) << "SceneCount : " << m_SceneFilename;

		// Parse the XML into the property tree.
		pt::read_json(m_SceneFilename, tree);

		// Use the default-value version of get to find the debug level.
		// Note that the default value is used to deduce the target type.
		m_EntitiesCount = tree.get("Scene.Count", 0);
		m_SceneName = tree.get<std::string>("Scene.Name", "unknown");
		
		Utils::GL::CheckGLState("Distance Field Start");

		std::string m_DefaultProgramName = tree.get<std::string>("Scene.Default Program", "Unknown");

		//BOOST_LOG_TRIVIAL(info) << "SceneCount : " << m_EntitiesCount;
		//BOOST_LOG_TRIVIAL(info) << "Scene Name : " << m_SceneName;

		// Use get_child to find the node containing the modules, and iterate over
		// its children. If the path cannot be resolved, get_child throws.
		// A C++11 for-range loop would also work.
		//BOOST_LOG_TRIVIAL(info) << "Number of Program Shaders : " << tree.get_child("Scene.Programs").size();
		BOOST_FOREACH(pt::ptree::value_type &v, tree.get_child("Scene.Programs")) {
			nxProgram* prog = new nxProgram(v.second.get_child("Shaders").size());
			//static_assert(nox::interfaces::IsResource<int>::value);
			m_pEngine->ResourceManager()->AddResource(prog);
			//m_pEngine->ResourceManager()->AddResource(new std::string("s"));

			prog->SetName(v.second.get<std::string>("Name"));
			bool use = false;
			if (std::strcmp(m_DefaultProgramName.c_str(), v.second.get<std::string>("Name").c_str()) == 0)
				use = true;
			BOOST_FOREACH(pt::ptree::value_type &v1, v.second.get_child("Shaders")) {
				BOOST_FOREACH(pt::ptree::value_type &v2, v1.second) {
					nxShaderLoaderBlob* data = new nxShaderLoaderBlob(m_pEngine, prog, v2.second.data(), gc_TypeMappings.at(v2.first.data()), -1, use);
					m_pEngine->Scheduler()->ScheduleJob((nxJob*)nxJobFactory::CreateJob(NX_JOB_LOAD_SHADER, data));
					BOOST_LOG_TRIVIAL(info) << "Shader : " << v2.second.data();
				}
			}
		}
		
		glm::uvec3 dimensions(tree.get_child("Scene.Voxelizer").get<int>("DimX", 128),
			tree.get_child("Scene.Voxelizer").get<int>("DimY", 128),
			tree.get_child("Scene.Voxelizer").get<int>("DimZ", 128));

		glm::uvec2 rsmDim(tree.get_child("Scene.RSM").get<int>("DimX", 128),
			tree.get_child("Scene.RSM").get<int>("DimY", 128));

		glm::uvec2 rmVDim(tree.get_child("Scene.RayMarcher").get<int>("VDimX", 16),
			tree.get_child("Scene.RayMarcher").get<int>("VDimY", 16));

		Utils::GL::CheckGLState("Scene Start");
		
		nxVoxelizer* voxel = new nxVoxelizer(m_pEngine, dimensions.x);
		nxRSM* rsm = new nxRSM(rsmDim.x, rsmDim.y);
		nxDistanceField* df = new nxDistanceField(4);
		nxRayMarcher* raym = new nxRayMarcher(voxel, df, rmVDim.x, rmVDim.y);

		//df->Init(dimensions.x, dimensions.y, dimensions.z);
		m_pEngine->Renderer()->SetVoxelizer(voxel);
		m_pEngine->Renderer()->SetDistanceField(df);
		m_pEngine->Renderer()->SetRSM(rsm);
		m_pEngine->Renderer()->SetRayMarcher(raym);

		nxVoxelizerInitializerBlob* voxelData = new nxVoxelizerInitializerBlob(m_pEngine, dimensions);
		m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_VOXELIZER_INIT, voxelData));

		nxDistanceFieldInitializerBlob* dfData = new nxDistanceFieldInitializerBlob(m_pEngine, dimensions);
		m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_DISTANCE_FIELD_INIT, dfData));

		nxRSMInitializerBlob* rsmData = new nxRSMInitializerBlob(m_pEngine, rsmDim);
		m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_RSM_INIT, rsmData));

		nxRayMarcherInitializerBlob* rmData = new nxRayMarcherInitializerBlob(m_pEngine, rmVDim);
		m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_RAY_MARCHER_INIT, rmData));

		static const nxFloat32 jump = 20.0f;
		static const nxFloat32 skip = 20.0f;
		// Memory backed models
		auto buffer = new std::vector<glm::vec3>;
		//auto texBuffer = new std::vector<glm::vec2>;
		buffer->push_back(glm::vec3(0.5, 0.8, 0.01));
		//texBuffer->push_back(glm::vec2(0, 0));
		buffer->push_back(glm::vec3(0.9, 0.8, 0.01));
		//texBuffer->push_back(glm::vec2(1, 0));
		buffer->push_back(glm::vec3(0.9, 0.3, 0.01));
		//texBuffer->push_back(glm::vec2(1, 1));
		buffer->push_back(glm::vec3(0.9, 0.3, 0.01));
		//texBuffer->push_back(glm::vec2(1, 1));
		buffer->push_back(glm::vec3(0.5, 0.3, 0.01));
		//texBuffer->push_back(glm::vec2(0, 1));
		buffer->push_back(glm::vec3(0.5, 0.8, 0.01));
		//texBuffer->push_back(glm::vec2(0, 0));

		std::vector<glm::vec3>* pixels = new std::vector<glm::vec3>;

		static const float offseting = 10.0f;
		static const int TEST_WIDTH = 16;
		static const int TEST_HEIGHT = 16;
		for (int i = 0; i < TEST_WIDTH; i++) {
			float u = (i + 0.5f) / (TEST_WIDTH);
			for (int j = 0; j < TEST_HEIGHT; j++) {
				float v = (j + 0.5f) / (TEST_WIDTH);
				glm::vec2 uv(u, v);
				uv = 2.0f * uv - 1.0f;
				glm::vec3 orient = glm::vec3(1.0 * offseting, jump - uv.y * offseting, -uv.x * offseting);
				pixels->emplace_back(0, jump, 0);
				pixels->push_back(orient);
			}
		}
		for (int i = 0; i < TEST_WIDTH; i++) {
			float u = (i + 0.5f) / (TEST_WIDTH);
			for (int j = 0; j < TEST_HEIGHT; j++) {
				float v = (j + 0.5f) / (TEST_WIDTH);
				glm::vec2 uv(u, v);
				uv = 2.0f * uv - 1.0f;
				glm::vec3 orient = glm::vec3(-1.0 * offseting, jump - uv.y * offseting, uv.x * offseting);
				pixels->emplace_back(0, jump, 0);
				pixels->push_back(orient);
			}
		}

		std::vector<glm::vec3>* sphere = new std::vector<glm::vec3>;
		//*sphere = Utils::Shape::generateSphereMesh(8, 8);
		*sphere = Utils::Shape::generateSphereMeshAt(8, 8, glm::vec3(0, 10, 0) );

		nxGLBufferedAssetLoaderBlob* bufferData = new nxGLBufferedAssetLoaderBlob(m_pEngine, buffer->data(), buffer->size());
		//m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_BUFF_ASSET, bufferData));
		//m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_PREVIEW_ASSET, bufferData));
		//m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_DEBUG_ASSET, &pixels->at(0)));

		//BOOST_LOG_TRIVIAL(info) << "Number of Lights : " << tree.get_child("Scene.Lights").size();
		BOOST_FOREACH(pt::ptree::value_type &v, tree.get_child("Scene.Lights")) {
			//BOOST_LOG_TRIVIAL(info) << "Camera X position : " << v.second.get<float>("CenterX", 0.0f);
			//BOOST_LOG_TRIVIAL(info) << "Camera Y position : " << v.second.get<float>("CenterY", 0.0f);
			//BOOST_LOG_TRIVIAL(info) << "Camera Z position : " << v.second.get<float>("CenterZ", 0.0f);
			//BOOST_LOG_TRIVIAL(info) << "View X position : " << v.second.get<float>("ViewX", 0.0f);
			//BOOST_LOG_TRIVIAL(info) << "View Y position : " << v.second.get<float>("ViewY", 0.0f);
			//BOOST_LOG_TRIVIAL(info) << "View Z position : " << v.second.get<float>("ViewZ", 0.0f);

			nxAssetLoaderBlob* data = new nxAssetLoaderBlob(
				m_pEngine,
				"Models/Cube",
				".3ds",
				glm::vec3(v.second.get<float>("CenterX", 0.0f),
				v.second.get<float>("CenterY", 0.0f),
				v.second.get<float>("CenterZ", 0.0f)),
				v.second.get<float>("Scale", 1.0f));

			//m_pEngine->Scheduler()->ScheduleJob((nxJob*)nxJobFactory::CreateJob(NX_JOB_LOAD_ASSET, data));
			
		}

		//BOOST_LOG_TRIVIAL(info) << "Number of Entities : " << tree.get_child("Scene.Entities").size();
		BOOST_FOREACH(pt::ptree::value_type &v, tree.get_child("Scene.Entities")) {
			BOOST_LOG_TRIVIAL(info) << "STUFF : " << v.second.get("ModelName", "unknkown");
			BOOST_LOG_TRIVIAL(info) << "Camera X position : " << v.second.get<float>("CenterX", 0.0f);
			BOOST_LOG_TRIVIAL(info) << "Camera Y position : " << v.second.get<float>("CenterY", 0.0f);
			BOOST_LOG_TRIVIAL(info) << "Camera Z position : " << v.second.get<float>("CenterZ", 0.0f);
			nxAssetLoaderBlob* data = new nxAssetLoaderBlob(
				m_pEngine,
				v.second.get("ModelName", "unknkown"),
				v.second.get("ModelType", "unknkown"),
				glm::vec3(v.second.get<float>("CenterX", 0.0f),
						  v.second.get<float>("CenterY", 0.0f),
						  v.second.get<float>("CenterZ", 0.0f)),
						  v.second.get<float>("Scale", 1.0f));
			
			m_pEngine->Scheduler()->ScheduleJob((nxJob*)nxJobFactory::CreateJob(NX_JOB_LOAD_ASSET, data));
		}

		m_Camera = new nxArcballCamera();
		m_Camera->SetPosition(tree.get_child("Scene.Camera").get<float>("PositionX", 0.0f),
			tree.get_child("Scene.Camera").get<float>("PositionY", 0.0f),
			tree.get_child("Scene.Camera").get<float>("PositionZ", 0.0f));
		m_Camera->SetViewTransform(
			glm::lookAt(
				glm::vec3(0, 20, 0),
				glm::vec3(0,0,0),
				glm::vec3(0,0, 1)
			)
		);

		nxLight* l_Light = new nxSpotlight(
            glm::vec3(0.0f, 3.0f, 0.0f),
			glm::lookAt(
                glm::vec3(0, 3, 0),
                glm::vec3(0, 0, -100),
                glm::vec3(0, 1, 0)
			));

		m_Lights.push_back(l_Light);

		//SetProjection(30.0f, 512.0f / 512, 1.0f, 1000.0f);
	
	} catch (std::exception const& e)
	{
		BOOST_LOG_TRIVIAL(error) << "Scene parse error : " << e.what();
	}
}

glm::mat3& nxScene::Normal() {
	m_MState.m_NMatrix = glm::mat3(m_MState.m_VMatrix);
	m_MState.m_NMatrix = glm::inverse(m_MState.m_NMatrix);
	m_MState.m_NMatrix = glm::transpose(m_MState.m_NMatrix);

	return m_MState.m_NMatrix;
}

bool errorGL = true;

void nxScene::CaptureRSM() {
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);
	glDisable(GL_CULL_FACE);

    for (auto light : m_Lights) {
        nxProgram* l_Prog = m_pEngine->Renderer()->GetActiveProgramByName("RSM");

		if (l_Prog == nullptr) break;

        m_pEngine->Renderer()->RSM()->Bind();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glViewport(0, 0, 512, 512);

        //printf("Luda OUT");
        if (l_Prog) {
            //printf("Luda IN");
            l_Prog->Use();
            glm::mat4 l_ProjRSM = glm::perspective(45.0f, 512.0f / 512, 5.0f, 300.0f);
            for (auto entity : m_Entities) {
                m_MState.m_VMatrix = light->View();
                m_MState.m_VMatrix = glm::translate(View(), entity->ModelTransform());

                //m_MState.m_VMatrix = glm::translate(View(), m_Entities[i]->ModelTransform());
                l_Prog->SetUniform("NormalMatrix", Normal());
                l_Prog->SetUniform("MVP", l_ProjRSM*m_MState.m_VMatrix);
				l_Prog->SetUniform("DiffuseTexture", NOX_DIFFUSE_MAP);
				//l_Prog->SetUniform("u_LightPos", NOX_DIFFUSE_MAP);
				//l_Prog->SetUniform("u_LightViewDir", NOX_DIFFUSE_MAP);
				l_Prog->SetUniform("u_LightFOV", 45);
				l_Prog->SetUniform("u_VPort", glm::ivec2(512,512));
				l_Prog->SetUniform("u_ZFar", 300.0f);
				l_Prog->SetUniform("u_ZNear", 5.0f);

				entity->Draw();
            }
			if (errorGL) {
				nxFloat32* buffer = new nxFloat32[512 * 512];
				glBindTexture(GL_TEXTURE_2D, m_pEngine->Renderer()->RSM()->ShadowMap());
				glGetTextureImage(m_pEngine->Renderer()->RSM()->ShadowMap(), 0, GL_DEPTH_COMPONENT, GL_FLOAT, 512 * 512 * 4, buffer);

				FIBITMAP* Image = FreeImage_ConvertFromRawBitsEx(true, (BYTE*)buffer, FIT_FLOAT, 512, 512, 4 * 512, 32, 0, 0, 0, false);
				FreeImage_Save(FIF_TIFF, Image, "shadow_map.tiff", 0);
				
				FIBITMAP* bitmap = FreeImage_Allocate(512, 512, 24);
				RGBQUAD color;

				nxFloat32 l_DepthMax = buffer[0];
				nxFloat32 l_DepthMin = buffer[0];
				nxFloat32 l_DepthRange = 0.0f;

				for (int i = 0; i < 512; i++) {
					if (buffer[i] > l_DepthMax)
						l_DepthMax = buffer[i];
				}

				for (int i = 0; i < 512; i++) {
					if (buffer[i] < l_DepthMin)
						l_DepthMin = buffer[i];
				}

				l_DepthRange = l_DepthMax - l_DepthMin;
				//l_DepthMin -= 0.01;


				std::cout << "Depth Max " << l_DepthMax << std::endl;
				std::cout << "Depth Min " << l_DepthMin << std::endl;
				std::cout << "Depth Range " << l_DepthRange << std::endl;

				for (int i = 0; i < 512; i++) {
					for (int j = 0; j < 512; j++) {
						//printf("   %f   ", buffer[i * 512 + j], buffer[i * 512 + j], buffer[i * 512 + j]);
						color.rgbRed = ((buffer[i * 512 + j] - l_DepthMin) / (l_DepthRange)) * 255;
						color.rgbGreen = ((buffer[i * 512 + j] - l_DepthMin) / (l_DepthRange)) * 255;
						color.rgbBlue = ((buffer[i * 512 + j] - l_DepthMin) / (l_DepthRange)) * 255;
						FreeImage_SetPixelColor(bitmap, j, i, &color);
					}
				}

				if (FreeImage_Save(FIF_TARGA, bitmap, "test.tga", 0))
					std::cout << "Image succesful" << std::endl;

				if (FreeImage_Save(FIF_PNG, bitmap, "test.png", 0))
					std::cout << "Image succesful" << std::endl;

				FreeImage_Unload(Image);

				Image = FreeImage_Allocate(512, 512, 24);

				glm::u8vec3* bufferNorm = new glm::u8vec3[512 * 512];

				glBindTexture(GL_TEXTURE_2D, m_pEngine->Renderer()->RSM()->NormalMap());
				glGetTextureImage(m_pEngine->Renderer()->RSM()->NormalMap(), 0, GL_RGB, GL_UNSIGNED_BYTE, 512 * 512 * 3, bufferNorm);

				for (int i = 0; i < 512; i++) {
					for (int j = 0; j < 512; j++) {
						//printf("   %f   ", buffer[i * 512 + j], buffer[i * 512 + j], buffer[i * 512 + j]);
						glm::u8vec3& current = bufferNorm[i * 512 + j];

						color.rgbRed = current.x;
						color.rgbGreen = current.y;
						color.rgbBlue = current.z;
						FreeImage_SetPixelColor(Image, j, i, &color);
					}
				}

				if (FreeImage_Save(FIF_TARGA, Image, "test2.tga", 0))
					std::cout << "Image succesful" << std::endl;

				if (FreeImage_Save(FIF_PNG, Image, "test2.png", 0))
					std::cout << "Image succesful" << std::endl;

				FreeImage_DeInitialise();

				Utils::GL::CheckGLState("Program USE");
				if (errorGL) {
					for (int i = 0; i < 512; i++) {
						for (int j = 0; j < 512; j++) {
							//printf("   %f   ", buffer[i * 512 + j], buffer[i * 512 + j], buffer[i * 512 + j]);
						}
						//printf("\n");
					}
				}
				delete[] buffer;
			}
			errorGL = false;
        }
        
    }

	glDisable(GL_CULL_FACE);
}

void nxScene::Draw() {
	if (CameraReady())
		m_MState.m_RMatrix = Camera()->Update();
	else
		m_MState.m_RMatrix = glm::mat4();

	m_MState.m_VMatrix = glm::mat4();

	//if (errorGL) Utils::GL::CheckGLState("Program USE");

	m_pEngine->Renderer()->UseProgram();

	for (size_t i = 0; i < m_Entities.size(); i++) {
			m_MState.m_VMatrix = glm::mat4();

		m_MState.m_VMatrix = glm::translate(View(),
			-m_Camera->Position());
		//m_MState.m_VMatrix = m_Camera->ViewTransform();
		m_MState.m_VMatrix = glm::translate(View(), m_Entities[i]->ModelTransform());
		m_MState.m_VMatrix *= m_MState.m_RMatrix;

		m_MState.m_MMatrix = glm::translate(glm::mat4(), m_Entities[i]->ModelTransform());

		//m_MState.m_VMatrix = glm::translate(View(), m_Entities[i]->ModelTransform());
		//m_pEngine->Renderer()->Program()->SetUniform("NormalMatrix", Normal());
		
		//if (errorGL) Utils::GL::CheckGLState("Set Normal");

        m_pEngine->Renderer()->Program()->SetUniform("MVP", m_MState.m_PMatrix*m_MState.m_VMatrix);
        m_pEngine->Renderer()->Program()->SetUniform("DiffuseTexture", NOX_DIFFUSE_MAP);
        //m_pEngine->Renderer()->Program()->SetUniform("MVP", View());
		//if (errorGL) Utils::GL::CheckGLState("Set MVP");

		m_Entities[i]->Draw();
		//if (errorGL) Utils::GL::CheckGLState("Draw : " + i);

	}

	nxProgram* l_Prog = m_pEngine->Renderer()->GetActiveProgramByName("Debug");
	
	if (l_Prog) {
		
		l_Prog->Use();
		for (auto entity : m_DebugEntities) {
			//printf("Luda IN");
			m_MState.m_VMatrix = glm::mat4();

			m_MState.m_VMatrix = glm::translate(View(),
				-m_Camera->Position());
			//m_MState.m_VMatrix = m_Camera->ViewTransform();

			m_MState.m_VMatrix *= m_MState.m_RMatrix;

			l_Prog->SetUniform("MVP", m_MState.m_PMatrix*m_MState.m_VMatrix);
			//m_pEngine->Renderer()->Program()->SetUniform("MVP", View());
			//if (errorGL) Utils::GL::CheckGLState("Set MVP");

			entity->LineDraw();
		}
	}
	
	//nxProgram* l_Prog = m_pEngine->Renderer()->GetActiveProgramByName("Debug");
	m_pEngine->Renderer()->UseProgram();

	glDisable(GL_DEPTH_TEST);
	//if (l_Prog) {

		//l_Prog->Use();
		for ( auto entity : m_PreviewEntities ) {
			//printf("Luda IN");
			m_MState.m_VMatrix = glm::mat4();

			m_MState.m_VMatrix = glm::translate(View(),
				-glm::vec3(0,0,0.01f));
			//m_MState.m_VMatrix = m_Camera->ViewTransform();

			//m_MState.m_VMatrix *= m_MState.m_RMatrix;
			glm::mat4 pMa = glm::ortho(-1, 1, -1, 1);
			m_pEngine->Renderer()->Program()->SetUniform("MVP", pMa * m_MState.m_VMatrix);
			//m_pEngine->Renderer()->Program()->SetUniform("MVP", View());
			//if (errorGL) Utils::GL::CheckGLState("Set MVP");

			entity->Draw();
		}
	//}
	glEnable(GL_DEPTH_TEST);

	//errorGL = false;
}

static bool print_dists = true;
void nxScene::DrawVoxelized() {
	if (CameraReady())
		m_MState.m_RMatrix = Camera()->Update();
	else
		m_MState.m_RMatrix = glm::mat4();

	m_MState.m_VMatrix = glm::mat4();

	m_pEngine->Renderer()->UseProgram();

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_pEngine->Renderer()->Voxelizer()->VoxelBuffer());

	//if (errorGL) Utils::GL::CheckGLState("Voxelized Program USE");

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_pEngine->Renderer()->Voxelizer()->VoxelBuffer());

	if (m_pEngine->Renderer()->VoxelizerReady()) {
		m_MState.m_PMatrix = m_pEngine->Renderer()->Voxelizer()->Projections()[2];
		m_pEngine->Renderer()->Voxelizer()->CalculateViewProjection();
		glViewportArrayv(0, 3, &m_pEngine->Renderer()->Voxelizer()->Viewports()[0][0]);
	}
	glm::vec3 l_Voxel = m_pEngine->Renderer()->Voxelizer()->GridSize() / glm::vec3(m_pEngine->Renderer()->Voxelizer()->Dimesions());
	for (size_t i = 0; i < m_Entities.size(); i++) {
		m_MState.m_VMatrix = glm::translate(View(),
			m_Camera->Position());

		m_MState.m_VMatrix = glm::translate(View(), m_Entities[i]->ModelTransform());
		//m_MState.m_VMatrix *= m_MState.m_RMatrix;

		//m_MState.m_MMatrix = glm::translate(View(), m_Entities[i]->ModelTransform());
		//m_pEngine->Renderer()->Program()->SetUniform("NormalMatrix", Normal());

		//if (errorGL) Utils::GL::CheckGLState("Set Normal");
		
		//m_pEngine->Renderer()->Program()->SetUniform("ModelMatrix", glm::mat4());
		//m_pEngine->Renderer()->Program()->SetUniform("ModelMatrix", glm::translate(glm::mat4(), m_Entities[i]->ModelTransform()));
		//m_pEngine->Renderer()->Program()->SetUniform("MVP", m_MState.m_PMatrix*m_MState.m_VMatrix);
		m_pEngine->Renderer()->Program()->SetUniform("GridSize", m_pEngine->Renderer()->Voxelizer()->Dimesions());
		m_pEngine->Renderer()->Program()->SetUniform("GridMin", m_pEngine->Renderer()->Voxelizer()->GridMin());
		m_pEngine->Renderer()->Program()->SetUniform("VoxelSize", l_Voxel);

		m_pEngine->Renderer()->Program()->SetUniform("ViewProjMatrix", 3, m_pEngine->Renderer()->Voxelizer()->ViewProjections());

		//m_pEngine->Renderer()->Program()->SetUniform("MVP", View());
		//if (errorGL) Utils::GL::CheckGLState("Set MVP");

		m_Entities[i]->Draw();
		//if (errorGL) Utils::GL::CheckGLState("Draw : " + i);

	}
	
	m_pEngine->Renderer()->GetActiveProgramByName("DistanceFieldInit")->Use();

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_pEngine->Renderer()->Voxelizer()->VoxelBuffer());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_pEngine->Renderer()->DistanceField()->DistanceFieldFrontBuffer());
	//glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_pEngine->Renderer()->DistanceField()->DistanceFieldBackBuffer());

	m_pEngine->Renderer()->GetActiveProgramByName("DistanceFieldInit")->SetUniform("u_Dim", m_pEngine->Renderer()->Voxelizer()->Dimesions());
	//m_pEngine->Renderer()->GetActiveProgramByName("DistanceField")->SetUniform("u_Truncation", 4);
	m_pEngine->Renderer()->GetActiveProgramByName("DistanceFieldInit")->SetUniform("u_VoxelSize", l_Voxel);

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_pEngine->Renderer()->DistanceField()->DistanceFieldBuffer());

	GLuint64 startTime, stopTime;
	unsigned int queryID[2];
	int stopTimerAvailable = 0;

	// generate two queries
	glGenQueries(2, queryID);

	glQueryCounter(queryID[0], GL_TIMESTAMP);

	m_pEngine->Renderer()->DistanceField()->Calculate(m_pEngine->Renderer()->Voxelizer()->VoxelBuffer());

	m_pEngine->Renderer()->GetActiveProgramByName("DistanceFieldStep")->Use();

	//m_pEngine->Renderer()->GetActiveProgramByName("DistanceFieldStep")->SetUniform("u_Dim", m_pEngine->Renderer()->Voxelizer()->Dimesions());
	//m_pEngine->Renderer()->GetActiveProgramByName("DistanceFieldStep")->SetUniform("u_VoxelSize", l_Voxel);
	for (int i = 1; i < 1; i++) {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_pEngine->Renderer()->DistanceField()->DistanceFieldFrontBuffer());
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_pEngine->Renderer()->DistanceField()->DistanceFieldBackBuffer());

		m_pEngine->Renderer()->DistanceField()->Calculate(m_pEngine->Renderer()->Voxelizer()->VoxelBuffer());

		m_pEngine->Renderer()->DistanceField()->SwapBuffers();
	}
	
	glQueryCounter(queryID[1], GL_TIMESTAMP);

	while (!stopTimerAvailable) {
		glGetQueryObjectiv(queryID[1],
			GL_QUERY_RESULT_AVAILABLE,
			&stopTimerAvailable);
	}

	// get query results
	glGetQueryObjectui64v(queryID[0], GL_QUERY_RESULT, &startTime);
	glGetQueryObjectui64v(queryID[1], GL_QUERY_RESULT, &stopTime);

	//printf("Time spent on the GPU: %f ms\n", (stopTime - startTime) / 1000000.0);
	
	m_pEngine->Renderer()->GetActiveProgramByName("RayMarch")->Use();

	m_pEngine->Renderer()->GetActiveProgramByName("RayMarch")->SetUniform("u_VPort", m_pEngine->Renderer()->RayMarcher()->VPort());
	m_pEngine->Renderer()->GetActiveProgramByName("RayMarch")->SetUniform("u_Dim", m_pEngine->Renderer()->Voxelizer()->Dimesions());
	l_Voxel = m_pEngine->Renderer()->Voxelizer()->GridSize() / glm::vec3(m_pEngine->Renderer()->Voxelizer()->Dimesions());
	m_pEngine->Renderer()->GetActiveProgramByName("RayMarch")->SetUniform("u_VoxelSize", l_Voxel);
	m_pEngine->Renderer()->GetActiveProgramByName("RayMarch")->SetUniform("u_GridMin", m_pEngine->Renderer()->Voxelizer()->GridMin());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_pEngine->Renderer()->Voxelizer()->VoxelBuffer());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_pEngine->Renderer()->DistanceField()->DistanceFieldFrontBuffer());
	//glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_pEngine->Renderer()->DistanceField()->DistanceFieldBackBuffer());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_pEngine->Renderer()->RayMarcher()->Buffer());

	if (m_pEngine->Renderer()->VoxelizerReady()) {
		m_pEngine->Renderer()->RayMarcher()->Calculate();
		
		if (print_dists) {
			//std::cout << "Pass " << l_Voxel.x << ", " << l_Voxel.y << ", " << l_Voxel.z << std::endl;
			glm::ivec2 l_VPort = m_pEngine->Renderer()->RayMarcher()->VPort();
			glm::vec3 l_TestPosition(0, 10, 0);
				glm::vec3 l_TestPositionA(0, 10, 0);
				glm::vec3 l_TestPositionB(0, 10, 0);
				glm::vec3 l_GridSize = m_pEngine->Renderer()->Voxelizer()->GridSize();
				glm::vec3 l_GridCenter = m_pEngine->Renderer()->Voxelizer()->GridCenter();
				glm::uvec3 l_Dims = m_pEngine->Renderer()->Voxelizer()->Dimesions();
				glm::vec3 l_GridMax = m_pEngine->Renderer()->Voxelizer()->GridMax();
				glm::vec3 l_GridMin = m_pEngine->Renderer()->Voxelizer()->GridMin();
				
				std::cout << "Grid Min is : " << l_GridMin.x << ", " << l_GridMin.y << ", " << l_GridMin.z << std::endl;
				std::cout << "Grid Max is : " << l_GridMax.x << ", " << l_GridMax.y << ", " << l_GridMax.z << std::endl;
				std::cout << "Grid Cen is : " << l_GridCenter.x << ", " << l_GridCenter.y << ", " << l_GridCenter.z << std::endl;
				//std::cout << "Grid Center is : " << ip[l_Indexes.x][l_Indexes.y][l_Indexes.z] << std::endl;

			ClearDebugEntities();

			/*
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_pEngine->Renderer()->DistanceField()->DistanceFieldFrontBuffer());

			nxFloat32* p2 = (nxFloat32*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
			//if (error) Utils::GL::CheckGLState("Frame");
			int countVoxels = 0;
			int xCount = 0;
			int yCount = 0;
			int zCount = 0;
			if (p2) {
				typedef boost::multi_array_ref<nxFloat32, 3> array_type;
				typedef array_type::index index;

				glm::vec3 l_TestPosition(0, 10, 0);
				glm::vec3 l_TestPositionA(0, 10, 0);
				glm::vec3 l_TestPositionB(0, 10, 0);
				glm::vec3 l_GridSize = m_pEngine->Renderer()->Voxelizer()->GridSize();
				glm::vec3 l_GridCenter = m_pEngine->Renderer()->Voxelizer()->GridCenter();
				glm::uvec3 l_Dims = m_pEngine->Renderer()->Voxelizer()->Dimesions();
				glm::vec3 l_GridMax = m_pEngine->Renderer()->Voxelizer()->GridMax();
				glm::vec3 l_GridMin = m_pEngine->Renderer()->Voxelizer()->GridMin();
				glm::vec3 l_Voxel = l_GridSize / glm::vec3(l_Dims);

				array_type ip(p2, boost::extents[l_Dims.x][l_Dims.y][l_Dims.z]);

				l_TestPosition -= l_GridMin;
				glm::ivec3 l_Indexes = l_TestPosition / l_Voxel;

				std::cout << "place is : " << l_Indexes.x << ", " << l_Indexes.y << ", " << l_Indexes.z << std::endl;
				std::cout << "distance at that place is : " << ip[l_Indexes.x][l_Indexes.y][l_Indexes.z] << std::endl;

				for (int i = 0; i < l_Dims.x; i++) {
					for (int j = 0; j < l_Dims.y; j++) {
						for (int k = 0; k < l_Dims.z; k++) {
							if (ip[i][j][k] < 1) {
								//printf("%g ", i * voxel.x);
								//printf("%g ", j * voxel.y);
								//printf("%g = %g\n", k * voxel.z, ip[i][j][k]);
								std::vector<glm::vec3>* sphere = new std::vector<glm::vec3>;
								//*sphere = Utils::Shape::generateSphereMesh(8, 8);
								*sphere = Utils::Shape::generateSphereMeshAt(8, 8, glm::vec3(i * l_Voxel.x, j * l_Voxel.y, k * l_Voxel.z ));

								nxGLBufferedAssetLoaderBlob* bufferData = new nxGLBufferedAssetLoaderBlob(m_pEngine, sphere->data(), sphere->size());
								m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_DEBUG_ASSET, bufferData));
							}
						}
					}
				}
			}

			glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
			*/

			

			glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_pEngine->Renderer()->RayMarcher()->Buffer());

			glm::vec4* p = (glm::vec4*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);

			std::vector<glm::vec3>* pixels = new std::vector<glm::vec3>;
			for (int f = 3; f < 4; f++) {
				for (int i = 0; i < l_VPort.x; i++) {
					for (int j = 0; j < l_VPort.y; j++) {
						std::cout << "Face : " << f << " : " << p[f * l_VPort.x * l_VPort.y + i * l_VPort.y + j].x << ", " << p[f * l_VPort.x * l_VPort.y + i * l_VPort.y + j].y << ", " << p[f * l_VPort.x * l_VPort.y + i * l_VPort.y + j].z << ", " << p[f * l_VPort.x * l_VPort.y + i * l_VPort.y + j].w << std::endl;
						//glm::vec3 target = glm::vec3(p[f * l_VPort.x * l_VPort.y + i * l_VPort.y + j]);
						glm::vec3 target = glm::vec3(p[f * l_VPort.x * l_VPort.y + i * l_VPort.y + j]) * l_Voxel + m_pEngine->Renderer()->Voxelizer()->GridMin();
						//glm::vec3 orient = glm::vec3(target, jump - uv.y * offseting, uv.x * offseting);
						pixels->emplace_back(0, 7, 0);
						pixels->push_back(target);

						std::vector<glm::vec3>* sphereHit = new std::vector<glm::vec3>;
						//*sphere = Utils::Shape::generateSphereMesh(8, 8);
						glm::ivec3 l_VoxelCoordHit = glm::ivec3(target / l_Voxel);
						glm::vec3 l_FinalPosHit = glm::vec3(l_VoxelCoordHit) * l_Voxel;
						//*sphereHit = Utils::Shape::generateSphereMeshAt(8, 8, target);
						//*sphereHit = Utils::Shape::generateSphereMeshAt(8, 8, l_FinalPosHit);
						*sphereHit = Utils::Shape::generateSphereMeshScaledAt(8, 8, 1.0f / l_Voxel.x, l_FinalPosHit);
						
						nxGLBufferedAssetLoaderBlob* bufferDataHit = new nxGLBufferedAssetLoaderBlob(m_pEngine, sphereHit->data(), sphereHit->size());
						m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_DEBUG_ASSET, bufferDataHit));

					}
				}
			}

			//std::cout << "Number of lines " << pixels->size() << std::endl;
			//std::cout << "Marcher Port " << m_pEngine->Renderer()->RayMarcher()->VPort().x << std::endl;
			//std::cout << "Marcher Port " << m_pEngine->Renderer()->RayMarcher()->VPort().y << std::endl;

			nxGLBufferedAssetLoaderBlob* bufferDataA = new nxGLBufferedAssetLoaderBlob(m_pEngine, pixels->data(), pixels->size());
			m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_DEBUG_ASSET, bufferDataA));

			std::vector<glm::vec3>* sphere = new std::vector<glm::vec3>;
			//*sphere = Utils::Shape::generateSphereMesh(8, 8);
			l_TestPositionB -= l_GridMin;
			l_TestPositionB = l_TestPositionB / l_Voxel;
			glm::ivec3 l_VoxelCoord = glm::ivec3(l_TestPositionB);
			glm::vec3 l_FinalPos = glm::vec3(l_VoxelCoord) * l_Voxel + l_GridMin;
			*sphere = Utils::Shape::generateSphereMeshAt(8, 8, l_FinalPos);

			nxGLBufferedAssetLoaderBlob* bufferData = new nxGLBufferedAssetLoaderBlob(m_pEngine, sphere->data(), sphere->size());
			m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_DEBUG_ASSET, bufferData));
			//m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_DEBUG_ASSET, &pixels->at(0)));


			glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

			//m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_DEBUG_ASSET, &pixels->at(0)));
			//print_dists = false;

			//std::cout << "Pass " << p[0].x << ", " << p[0].y << ", " << p[0].z << ", " << p[0].w << std::endl;
			//std::cout << "Pass " << p[4] << ", " << p[5] << ", " << p[6] << std::endl;
			//std::cout << "Pass " << p[8] << ", " << p[9] << ", " << p[10] << std::endl;
            /*
			auto sphere = new std::vector<glm::vec3>;
			auto sphere2 = new std::vector<glm::vec3>;
			auto sphere3 = new std::vector<glm::vec3>;
			auto sphere4 = new std::vector<glm::vec3>;
			*sphere = Utils::Shape::generateSphereMeshAt(8, 8, glm::vec3(glm::vec3(p[0]) * l_Voxel + m_pEngine->Renderer()->Voxelizer()->GridMin()));
			*sphere2 = Utils::Shape::generateSphereMeshAt(8, 8, glm::vec3(glm::vec3(p[1]) * l_Voxel + m_pEngine->Renderer()->Voxelizer()->GridMin()));
			*sphere3 = Utils::Shape::generateSphereMeshAt(8, 8, glm::vec3(glm::vec3(p[257]) * l_Voxel + m_pEngine->Renderer()->Voxelizer()->GridMin()));
			*sphere4 = Utils::Shape::generateSphereMeshAt(8, 8, glm::vec3(p[3].x, p[3].y, p[3].z));
			//*sphere = Utils::Shape::generateSphereMeshAt(8, 8, glm::vec3(0, 10, 0));
            
			nxGLBufferedAssetLoaderBlob* bufferData = new nxGLBufferedAssetLoaderBlob(m_pEngine, sphere->data(), sphere->size());
			//m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_BUFF_ASSET, bufferData));
			nxGLBufferedAssetLoaderBlob* bufferData2 = new nxGLBufferedAssetLoaderBlob(m_pEngine, sphere2->data(), sphere2->size());
			//m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_BUFF_ASSET, bufferData2));
			nxGLBufferedAssetLoaderBlob* bufferData3 = new nxGLBufferedAssetLoaderBlob(m_pEngine, sphere3->data(), sphere3->size());
			//m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_BUFF_ASSET, bufferData3));
			nxGLBufferedAssetLoaderBlob* bufferData4 = new nxGLBufferedAssetLoaderBlob(m_pEngine, sphere4->data(), sphere4->size());
			//m_pEngine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_BUFF_ASSET, bufferData4));
            */
		}
		
	}
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
	errorGL = false;
	/*if (CameraReady())
		m_MState.m_RMatrix = Camera()->Update();
	else
		m_MState.m_RMatrix = glm::mat4();

	m_MState.m_VMatrix = glm::mat4();

	//glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_pEngine->Renderer()->m_ssbo);

	//m_pEngine->Renderer()->SetActiveProgramByName("Voxelize");
	m_pEngine->Renderer()->UseProgram();
	if (errorGL) Utils::GL::CheckGLState("Program USE");

	if (m_pEngine->Renderer()->VoxelizerReady()) {
		m_MState.m_PMatrix = m_pEngine->Renderer()->Voxelizer()->Projections()[2];
		m_pEngine->Renderer()->Voxelizer()->CalculateViewProjection();
		//glViewportArrayv(0, 3, &m_pEngine->Renderer()->Voxelizer()->Viewports()[0][0]);
	}

	for (size_t i = 0; i < m_Entities.size(); i++) {
		if (!m_pEngine->Renderer()->VoxelizerReady())
			m_MState.m_VMatrix = m_pEngine->Renderer()->Voxelizer()->Views()[0];
		else
			m_MState.m_VMatrix = glm::translate(View(),
				m_Camera->Position());

		m_MState.m_VMatrix = glm::translate(View(), m_Entities[i]->ModelTransform());
		//m_MState.m_VMatrix *= m_MState.m_RMatrix;

		// = glm::translate(View(), m_Entities[i]->ModelTransform());
		//m_pEngine->Renderer()->Program()->SetUniform("NormalMatrix", Normal());
		m_pEngine->Renderer()->Program()->SetUniform("uniform_model", View());
		//m_pEngine->Renderer()->Program()->SetUniform("uniform_size", m_pEngine->Renderer()->Voxelizer()->Dimesions());
		if (errorGL) Utils::GL::CheckGLState("Set Normal");

		//m_pEngine->Renderer()->Program()->SetUniform("uniform_view_proj", 3, m_pEngine->Renderer()->Voxelizer()->ViewProjections());
		//m_pEngine->Renderer()->Program()->SetUniform("MVP", View());
		if (errorGL) Utils::GL::CheckGLState("Set MVP");

		m_Entities[i]->Draw();
		if (errorGL) Utils::GL::CheckGLState("Draw : " + i);

	}

	//glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	//if (m_pEngine->Renderer()->VoxelizerReady()) 
	//	m_pEngine->Renderer()->Voxelizer()->PrintGrid();

	//errorGL = false;
	*/
}

void nxScene::UpdateBounds(nxEntity* ent) {

	static const float g_GridOffset = .1f;

	if (ent->MinX() < m_GMinX) m_GMinX = ent->MinX() - g_GridOffset;
	if (ent->MinY() < m_GMinY) m_GMinY = ent->MinY() - g_GridOffset;
	if (ent->MinZ() < m_GMinZ) m_GMinZ = ent->MinZ() - g_GridOffset;
	if (ent->MaxX() > m_GMaxX) m_GMaxX = ent->MaxX() + g_GridOffset;
	if (ent->MaxY() > m_GMaxY) m_GMaxY = ent->MaxY() + g_GridOffset;
	if (ent->MaxZ() > m_GMaxZ) m_GMaxZ = ent->MaxZ() + g_GridOffset;

}

void nxScene::SetProjection(float angle, float fov, float zNear, float zFar) {
	m_MState.m_PMatrix = glm::perspective(angle, fov, zNear, zFar);
}

void nxScene::SetProjection(glm::mat4& proj) {
	m_MState.m_PMatrix = proj;
}

bool nxSceneLoader::operator()(void* data) {
	nxScene* scene = NOX_ENGINE_GET(Scene);

	scene->Init();

	return true;
}