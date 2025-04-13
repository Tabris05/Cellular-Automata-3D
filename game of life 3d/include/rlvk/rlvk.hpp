#ifndef RLVK_H
#define RLVK_H

#include "rldefs.hpp"

void SetConfigFlags(unsigned int flags);                    // Setup init configuration flags (view FLAGS)
void SetTargetFPS(int fps);                                 // Set target FPS (maximum)

void InitWindow(int width, int height, const char* title);  // Initialize window and OpenGL context
bool WindowShouldClose(void);                               // Check if KEY_ESCAPE pressed or Close icon pressed
void CloseWindow(void);                                     // Close window and unload OpenGL context

bool IsKeyPressed(int key);                             // Check if a key has been pressed once
bool IsKeyDown(int key);                                // Check if a key is being pressed
bool IsMouseButtonPressed(int button);                  // Check if a mouse button has been pressed once
Vector2 GetMouseDelta(void);                            // Get mouse delta between frames
float GetMouseWheelMove(void);                          // Get mouse wheel movement Y

Matrix GetCameraMatrix(Camera camera);                      // Get camera transform matrix (view matrix)

void ClearBackground(Color color);                          // Set background color (framebuffer clear color)
void BeginDrawing(void);                                    // Setup canvas (framebuffer) to start drawing
void EndDrawing(void);                                      // End canvas drawing and swap buffers (double buffering)
void BeginMode3D(Camera3D camera);                          // Begin 3D mode with custom camera (3D)
void EndMode3D(void);                                       // Ends 3D mode and returns to default 2D orthographic mode

void rlEnableBackfaceCulling(void);               // Enable backface culling

void DrawCube(Vector3 position, float width, float height, float length, Color color);             // Draw cube
void DrawCubeWires(Vector3 position, float width, float height, float length, Color color);        // Draw cube wires
void DrawFPS(int posX, int posY);                                                     // Draw current FPS

float Vector3DotProduct(Vector3 v1, Vector3 v2);

#endif
