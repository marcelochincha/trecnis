#pragma once

#include <math/sr_math.hpp>









struct camera
{
    mutable vec3 _position;
    mutable vec3 _rotation;
    float _fov;
    float _aspectRatio;
    float _nearPlane;
    float _farPlane;
    mutable mat4 _viewMatrix;
    mutable mat4 _projectionMatrix;
    mutable mat4 _rotationMatrix;
    mutable bool _viewDirty = true;
    mutable bool _projectionDirty = true;
    mutable bool _rotationMatrixDirty = true;

    void recalculateRotationMatrix() const;
    void recalculateViewMatrix() const;
    void recalculateProjectionMatrix() const;

    camera();
    camera(vec3 position, vec3 rotation, float fov, float aspectRatio, float nearPlane, float farPlane);


    const mat4 &view() const;
    const mat4 &projection() const;
    const mat4 &rotation() const;
    const vec3 lookVector() const;
    const mat4 worldMatrix() const;


    void setPosition(const vec3 &p);
    void setRotation(const vec3 &r);
    void setFov(float f);


    void lookAt(const vec3 &target, const vec3 &up = vec3(0.0f, 1.0f, 0.0f));
};