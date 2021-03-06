#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

class nxEntity;

class nxLight {

private:

	glm::vec3 m_Center;
	glm::vec3 m_At;
	glm::vec3 m_Color;
	nxFloat32 m_LinearAtt;
	glm::vec3 m_LightDir;
	glm::mat4 m_Transformation;
    nxEntity* m_LightEnt;

public:

	nxLight();
    nxLight(glm::vec3 position, glm::mat4 transform) : m_Center(position), m_Transformation(transform), m_Color(glm::vec3(1, 1, 1)), m_LinearAtt(0.1f) { };

	glm::mat4 View() { return m_Transformation; }
	glm::vec3 Center() { return m_Center; }
	glm::vec3 At() { return m_At; }
	glm::vec3 At(glm::vec3 newAt) { return m_At; }
	void Center(glm::vec3 newCenter) {
        m_Center = newCenter;
        m_Transformation = glm::lookAt(
            m_Center,
			m_Center + glm::vec3(100, -50, -100),
            glm::vec3(0, 1, 0)
        );
		LightDir(m_Center + glm::vec3(100, -50, -100) - m_Center);
	}

    void CenterX(float newX) {
        m_Center.x = newX;
        m_Transformation = glm::lookAt(
            m_Center,
			m_Center + glm::vec3(100, -50, -100),
            glm::vec3(0, 1, 0)
        );
		LightDir(m_Center + glm::vec3(100, -50, -100) - m_Center);
	}
    void CenterY(float newY) { 
        m_Center.y = newY;
        m_Transformation = glm::lookAt(
            m_Center,
			m_Center + glm::vec3(100, -50, -100),
            glm::vec3(0, 1, 0)
        );
		LightDir(m_Center + glm::vec3(100, -50, -100) - m_Center);
	}
    void CenterZ(float newZ) { 
		//float offZ = m_Center.z - newZ;
		//m_At.z += offZ;
        m_Center.z = newZ;
        m_Transformation = glm::lookAt(
            m_Center,
			m_Center + glm::vec3(100, -50, -100),
            glm::vec3(0, 1, 0)
       );
		LightDir(m_Center + glm::vec3(100, -50, -100) - m_Center);

    }
    nxEntity* Entity() { return m_LightEnt; }
	glm::vec3 LightDir() {
		return m_LightDir;
	}

	void LightDir(glm::vec3 lightDir) {
		m_LightDir = glm::normalize(lightDir);
	}
};

class nxSpotlight : public nxLight {

private:

    nxFloat32   m_ConeAngle;

public:

	nxSpotlight();
	nxSpotlight(glm::vec3 position, glm::mat4 transform) : nxLight(position, transform), m_ConeAngle(45.0f) { };

    nxFloat32 ConeAngle() { return m_ConeAngle; }
};