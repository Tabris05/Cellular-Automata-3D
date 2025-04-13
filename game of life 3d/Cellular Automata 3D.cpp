// game of life 3d.cpp
// by Adam Ullmann

#include <rlvk/rlvk.hpp>
#include <math.h>
#include <iostream>
#include <vector>

const int screenWidth = 1600;
const int screenHeight = 950;
const float cellSize = 1.0f;
const int gridWidth = 50;
const int gridHeight = 50;
const int gridDepth = 50;
bool grid[gridWidth][gridHeight][gridDepth] = { 0 };
bool nextGrid[gridWidth][gridHeight][gridDepth] = { 0 };

/*
void DrawShadow(const Camera3D& camera, const Vector3& lightPosition) {



    for (int z = 0; z < gridDepth; z++) {
        for (int y = 0; y < gridHeight; y++) {
            for (int x = 0; x < gridWidth; x++) {
                if (grid[x][y][z]) {
                    Vector3 cubePosition = { x * cellSize, y * cellSize, z * cellSize };
                    DrawCube(cubePosition, cellSize, cellSize, cellSize, Fade(BLACK, 0.5f));
                }
            }
        }
    }


}       */


float CalculateGradient(int x, int y, int z) {
    float centerX = gridWidth / 2.0f;
    float centerY = gridHeight / 2.0f;
    float centerZ = gridDepth / 2.0f;

    float distance = sqrtf((x - centerX) * (x - centerX) +
        (y - centerY) * (y - centerY) +
        (z - centerZ) * (z - centerZ));

    float maxDistance = sqrtf(centerX * centerX + centerY * centerY + centerZ * centerZ);

    return distance / maxDistance;
}

bool IsCubeInFrustum(const Vector3& position, float size, const Matrix& projview) {
    Vector4 planes[6];

    // Extract frustum planes from the combined projection and view matrix
    planes[0] = { projview.m3 + projview.m0, projview.m7 + projview.m4, projview.m11 + projview.m8, projview.m15 + projview.m12 }; // Left
    planes[1] = { projview.m3 - projview.m0, projview.m7 - projview.m4, projview.m11 - projview.m8, projview.m15 - projview.m12 }; // Right
    planes[2] = { projview.m3 + projview.m1, projview.m7 + projview.m5, projview.m11 + projview.m9, projview.m15 + projview.m13 }; // Bottom
    planes[3] = { projview.m3 - projview.m1, projview.m7 - projview.m5, projview.m11 - projview.m9, projview.m15 - projview.m13 }; // Top
    planes[4] = { projview.m3 + projview.m2, projview.m7 + projview.m6, projview.m11 + projview.m10, projview.m15 + projview.m14 }; // Near
    planes[5] = { projview.m3 - projview.m2, projview.m7 - projview.m6, projview.m11 - projview.m10, projview.m15 - projview.m14 }; // Far

    // Check against the frustum planes
    for (int i = 0; i < 6; i++) {
        Vector3 normal = { planes[i].x, planes[i].y, planes[i].z };
        float distance = planes[i].w;
        float distanceToCenter = Vector3DotProduct(normal, position) + distance;
        if (distanceToCenter < -size * 250.0f )
            return false;
    }

    return true;
}



int min(int a, int b) {
    return (a < b) ? a : b;
}

int main() {

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Cellular Automata 3D");   // initialization
    unsigned int targetFPS = 60;
    SetTargetFPS(targetFPS);
    rlEnableBackfaceCulling();

    Camera3D camera = { 0 };
    Vector3 position = { 85.0f, 85.0f, 85.0f };
    camera.position = position;
    Vector3 target = { 25.0f, 25.0f, 25.0f };
    camera.target = target;     // camera looking at a point
    Vector3 up = { 0.0f, 1.0f, 0.0f };
    camera.up = up;          
    camera.fovy = 60.0f;                                
    camera.projection = CAMERA_PERSPECTIVE;             

    // arbitrary setting of live cells (we will change this later)
    grid[gridWidth / 2][gridHeight / 2][gridDepth / 2] = true;
    grid[gridWidth / 2 + 1][gridHeight / 2][gridDepth / 2] = true;
    grid[gridWidth / 2][gridHeight / 2 + 1][gridDepth / 2] = true;
    grid[gridWidth / 2][gridHeight / 2][gridDepth / 2 + 1] = true;//
    grid[gridWidth / 2 + 1][gridHeight / 2 + 1][gridDepth / 2] = true;
    grid[gridWidth / 2][gridHeight / 2 + 1][gridDepth / 2 + 1] = true;//
    grid[gridWidth / 2 + 1][gridHeight / 2][gridDepth / 2 + 1] = true;
    grid[gridWidth / 2 + 1][gridHeight / 2 + 1][gridDepth / 2 + 1] = true;



    float cameraAngleX = 0.0f;
    float cameraAngleY = 0.0f;
    float cameraZoom = 150.0f;

    bool drawCubes = true;
    bool drawWires = false;
    bool pause = false;

    
    // game loop
    while (!WindowShouldClose()) {
        // INPUT
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            drawCubes = !drawCubes; 
        }
        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
            drawWires = !drawWires;
        }
        if (IsKeyPressed(KEY_SPACE)) {
            pause = !pause; 
        }
        if (IsKeyDown(KEY_UP) && targetFPS < 240) {
            targetFPS += 1;
            SetTargetFPS(targetFPS);
        }
        else if (IsKeyDown(KEY_DOWN) && targetFPS > 1) {
            targetFPS -= 1;
            SetTargetFPS(targetFPS);
        }

        //UpdateCamera(&camera);

        Vector2 mouseDelta = GetMouseDelta();   // mouse movement input for camera control
        cameraAngleX += mouseDelta.x * 0.1f;
        cameraAngleY += mouseDelta.y * 0.1f;

        
        if (cameraAngleY > 89.9f)           // weird bug fix. limits camera y axis
            cameraAngleY = 89.9f;
        else if (cameraAngleY < -89.9f)
            cameraAngleY = -89.9f;

        // updates camera position and target based on camera angles
        camera.position.x = cosf(DEG2RAD * cameraAngleX) * cosf(DEG2RAD * cameraAngleY) * cameraZoom;
        camera.position.y = sinf(DEG2RAD * cameraAngleY) * 100.0f;
        camera.position.z = sinf(DEG2RAD * cameraAngleX) * cosf(DEG2RAD * cameraAngleY) * cameraZoom;
        Vector3 target = { 25.0f, 25.0f, 25.0f };
        camera.target = target;     // camera looking at a point in the center of the main cube

        // updates camera up vector based on camera angles
        //camera.up.x = cosf(DEG2RAD * cameraAngleX) * cosf(DEG2RAD * (cameraAngleY + 90.0f));
        //camera.up.y = sinf(DEG2RAD * (cameraAngleY + 90.0f));
        //camera.up.z = sinf(DEG2RAD * cameraAngleX) * cosf(DEG2RAD * (cameraAngleY + 90.0f));

        float mouseWheelMove = -GetMouseWheelMove();      // zoom controls
        cameraZoom += mouseWheelMove * 5.0f;
        if (cameraZoom < 1.0f)
            cameraZoom = 1.0f;

        //UpdateCamera(&camera);


        if (!pause) {




            // update
            for (int z = 0; z < gridDepth; z++) {
                for (int y = 0; y < gridHeight; y++) {
                    for (int x = 0; x < gridWidth; x++) {
                        int liveNeighbors = 0;

                        // check the 26 neighboring cells (it is constant even though it looks really bad)
                        for (int dz = -1; dz <= 1; dz++) {
                            for (int dy = -1; dy <= 1; dy++) {
                                for (int dx = -1; dx <= 1; dx++) {
                                    if (dz == 0 && dy == 0 && dx == 0)      // skip the current cell in focus
                                        continue;

                                    int nx = (x + dx + gridWidth) % gridWidth;
                                    int ny = (y + dy + gridHeight) % gridHeight;
                                    int nz = (z + dz + gridDepth) % gridDepth;

                                    if (grid[nx][ny][nz])
                                        liveNeighbors++;
                                }
                            }
                        }

                        // rules (hardcoded for now)
                        if (grid[x][y][z]) {
                            if (liveNeighbors == 6 || liveNeighbors == 11) {
                                nextGrid[x][y][z] = true;
                            }
                            else {
                                nextGrid[x][y][z] = false;
                            }
                        }
                        else {
                            if (liveNeighbors == 4) {
                                nextGrid[x][y][z] = true;
                            }
                            else {
                                nextGrid[x][y][z] = false;
                            }
                        }
                    }
                }
            }
            // next generation
            for (int z = 0; z < gridDepth; z++) {           
                for (int y = 0; y < gridHeight; y++) {
                    for (int x = 0; x < gridWidth; x++) {
                        grid[x][y][z] = nextGrid[x][y][z];
                    }
                }
            }
        }
        // start drawing section
        
            BeginDrawing();

            ClearBackground(RAYWHITE);
            
            BeginMode3D(camera);
            
            Vector3 positions2 = { gridWidth / 2, gridHeight / 2, gridDepth / 2 };
            DrawCubeWires(positions2, gridWidth, gridHeight, gridDepth, BLACK);


            //Vector3 cubePosition = { 0.0f, 0.0f, 0.0f };          // red dot on origin for debugging
            //DrawCube(cubePosition, 2.0f, 2.0f, 2.0f, RED);

            Matrix projview = GetCameraMatrix(camera);
            //OctreeNode* octreeRoot = BuildOctree(0, 0, 0, gridWidth, gridHeight, gridDepth);
                // drawing of cells
            int shadowIntensities[gridWidth][gridDepth] = {};
                for (int z = 0; z < gridDepth; z++) {
                    for (int y = 0; y < gridHeight; y++) {
                        for (int x = 0; x < gridWidth; x++) {
                            if (grid[x][y][z]) {
                                Vector3 cubePosition = { x * cellSize, y * cellSize, z * cellSize };
 
                                if (IsCubeInFrustum(cubePosition, cellSize, projview)) {
                                        float gradient = CalculateGradient(x, y, z);
                                        Color cellColor = Color{ unsigned char(30 * gradient), unsigned char(100 * gradient), unsigned char(255 * gradient), 255 };
                                        if (drawCubes) {
                                            DrawCube(cubePosition, cellSize, cellSize, cellSize, cellColor);
                                        }
                                        if (drawWires) {
                                            DrawCubeWires(cubePosition, cellSize, cellSize, cellSize, BLACK);
                                        }
                                        shadowIntensities[x][z] += 15;
                                }
                            }
                        }
                    }
                }
                
                
            
                for (int z = 0; z < gridDepth; z++) {
                    for (int x = 0; x < gridWidth; x++) {
                        if (shadowIntensities[x][z] > 0) {
                            Vector3 shadowPosition = { x * cellSize, 0.0f, z * cellSize };
                            DrawCube(shadowPosition, cellSize, 0.0f, cellSize, Color{ 0, 0, 0, (unsigned char)min(shadowIntensities[x][z], 255)});
                        }
                    }
                }
                
      
                
                        

        
        //std::cout << std::endl;
                //UnloadRenderTexture(offscreenFramebuffer);

        EndMode3D();
        DrawFPS(2, 2);
        EndDrawing();
        //end of draw
    }

    // De-Initialization
    CloseWindow();

    return 0;
}
