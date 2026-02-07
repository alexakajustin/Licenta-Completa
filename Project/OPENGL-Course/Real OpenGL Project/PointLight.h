#pragma once
#include "Light.h"

#include "OmniShadowMap.h"

class PointLight :
    public Light
{
public:
    PointLight();

    PointLight(GLfloat shadowWidth, GLfloat shadowHeight, 
        GLfloat near, GLfloat far,
        GLfloat red, GLfloat green, GLfloat blue,
        GLfloat ambientIntensity, GLfloat diffuseIntensity,
        GLfloat xPosition, GLfloat yPosition, GLfloat zPosition,
        GLfloat constant, GLfloat linear, GLfloat exponent);

    std::vector<glm::mat4> CalculateLightTransform();

    void UseLight(GLuint ambientIntensityLocation, GLuint ambientColourLocation,
        GLuint diffuseIntensityLocation, GLuint positionLocation, 
        GLuint constantLocation, GLuint linearLocation, GLuint exponentLocation);


    glm::vec3 GetPosition();
    GLfloat GetFarPlane();

    ~PointLight();
protected:
    // similar to a lightbulb(emitting light in all directions while being in a position)
    glm::vec3 position; // not direction cuz this is lighting in all directions
    //values for attenuation
    GLfloat constant, linear, exponent;

    GLfloat farPlane;

   
};

