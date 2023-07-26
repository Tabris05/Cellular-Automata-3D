// game of life 3d.cpp

#include "raylib.h"
#include <math.h>
#include <iostream>
//test
const int screenWidth = 1600;
const int screenHeight = 950;
const int gridSize = 1;
const int gridWidth = 50;
const int gridHeight = 50;
const int gridDepth = 50;
bool grid[gridWidth][gridHeight][gridDepth] = { 0 };
bool nextGrid[gridWidth][gridHeight][gridDepth] = { 0 };

int main() {
    InitWindow(screenWidth, screenHeight, "3D Game of Life");   // initialization
    SetTargetFPS(25);

    /*camera.position = {screenWidth / 2.0f, screenHeight / 2.0f, -screenWidth};
    camera.target = { screenWidth / 2.0f, screenHeight / 2.0f, 0 };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 90.0f;
    camera.projection = CAMERA_PERSPECTIVE;*/

    Camera3D camera = { 0 };
    Vector3 position = { 85.0f, 85.0f, 85.0f };
    camera.position = position;
    Vector3 target = { 0.0f, 0.0f, 0.0f };
    camera.target = target;     // camera looking at a point
    Vector3 up = { 0.0f, 1.0f, 0.0f };
    camera.up = up;          
    camera.fovy = 50.0f;                                
    camera.projection = CAMERA_PERSPECTIVE;             

    // we set a few cells as alive
    grid[gridWidth / 2][gridHeight / 2][gridDepth / 2] = true;
    grid[gridWidth / 2 + 1][gridHeight / 2][gridDepth / 2] = true;
    grid[gridWidth / 2][gridHeight / 2 + 1][gridDepth / 2] = true;
    grid[gridWidth / 2][gridHeight / 2][gridDepth / 2 + 1] = true;//
    grid[gridWidth / 2 + 1][gridHeight / 2 + 1][gridDepth / 2] = true;
    grid[gridWidth / 2][gridHeight / 2 + 1][gridDepth / 2 + 1] = true;//
    grid[gridWidth / 2 + 1][gridHeight / 2][gridDepth / 2 + 1] = true;
    grid[gridWidth / 2 + 1][gridHeight / 2 + 1][gridDepth / 2 + 1] = true;

    // game loop
    while (!WindowShouldClose()) {
        UpdateCamera(&camera);
        // update
        for (int z = 0; z < gridDepth; z++) {
            for (int y = 0; y < gridHeight; y++) {
                for (int x = 0; x < gridWidth; x++) {
                    int liveNeighbors = 0;

                    // check the 26 neighboring cells
                    for (int dz = -1; dz <= 1; dz++) {
                        for (int dy = -1; dy <= 1; dy++) {
                            for (int dx = -1; dx <= 1; dx++) {
                                // Skip the current cell
                                if (dz == 0 && dy == 0 && dx == 0)
                                    continue;

                                int nx = (x + dx + gridWidth) % gridWidth;
                                int ny = (y + dy + gridHeight) % gridHeight;
                                int nz = (z + dz + gridDepth) % gridDepth;

                                if (grid[nx][ny][nz])
                                    liveNeighbors++;
                            }
                        }
                    }

                    // the rules
                    if (grid[x][y][z]) {
                        if (liveNeighbors == 4 || liveNeighbors == 4) {
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


        // Draw
        BeginDrawing();

        ClearBackground(RAYWHITE);

        BeginMode3D(camera);
        Vector3 positions2 = { gridWidth/2, gridHeight/2, gridDepth/2 };
        DrawCubeWires(positions2, gridWidth, gridHeight, gridDepth, BLACK);
        //Vector3 cubePosition = { 0.0f, 0.0f, 0.0f };
        //DrawCube(cubePosition, 2.0f, 2.0f, 2.0f, RED);
 
        // Draw the cells
        for (int z = 0; z < gridDepth; z++) {
            for (int y = 0; y < gridHeight; y++) {
                for (int x = 0; x < gridWidth; x++) {
                    if (grid[x][y][z]) {
                        //Vector3 positions = { x * gridSize, y * gridSize, z * gridSize };
                        Vector3 positions = { x * 1, y * 1, z * 1};
                        DrawCube(positions, gridSize, gridSize, gridSize, BLUE);
                        DrawCubeWires(positions, gridSize, gridSize, gridSize, BLACK);
                        //std::cout << grid[x][y][z] << std::endl;
                    }
                }
            }
        }
        //std::cout << std::endl;
        

        EndMode3D();

        EndDrawing();
    }

    // De-Initialization
    CloseWindow();

    return 0;
}
