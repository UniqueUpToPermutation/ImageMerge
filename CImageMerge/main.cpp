#include <iostream>
#include <fstream>
#include <vector>
#include <assert.h>

#include "lodepng.h"
#include "CutGrid.h"

#define IMAGE_SOURCE_1 "goat2.png"
#define IMAGE_SOURCE_2 "cat.png"
#define IMAGE_OUTPUT "result.png"
#define STITCH_MARGIN 100
#define GRID_ID(x, y, width) 2 + y * width + x

using namespace std;

// Singleton class for computing the edge weights on the image grid
class EdgeCostSingleton
{
public:
	static const vector<vector<float> >* Image1;
	static const vector<vector<float> >* Image2;
	static int Margin;
	static double LargeNumber;

	static CapType EdgeCost(int row, int col, CutGrid::EDir dir) {
		// Edge weights on either side should be infinite
		if (col == 0 && (dir == CutGrid::DIR_NORTH || dir == CutGrid::DIR_SOUTH))
			return LargeNumber;
		else if (col == Margin - 1 && (dir == CutGrid::DIR_NORTH || dir == CutGrid::DIR_SOUTH))
			return LargeNumber;

		// Otherwise, take the sum of the absolute differences between the pixels
		int dx = 0;
		int dy = 0;
		int image1Offset = (*Image1)[0].size() - Margin;

		switch (dir)
		{
		case CutGrid::DIR_WEST:
			dx = -1;
			break;
		case CutGrid::DIR_EAST:
			dx = 1;
			break;
		case CutGrid::DIR_SOUTH:
			dy = 1;
			break;
		case CutGrid::DIR_NORTH:
			dy = -1;
			break;
		}

		int row2 = row + dy;
		int col2 = col + dx;

		double weight = 0.0;
		weight += std::abs((*Image1)[row][image1Offset + col] - (*Image2)[row2][col2]);
		weight += std::abs((*Image1)[row2][image1Offset + col2] - (*Image2)[row][col]);

		return weight;
	}
};

// Stitch two images together using basic min-cut method
void performStitching(const vector<vector<float> >& image1,
	const vector<vector<float> >& image2, 
	const int margin, vector<vector<float> >& output)
{
	int gridWidth = margin;
	int gridHeight = image1.size();

	// Prepare the singleton edge weight class
	EdgeCostSingleton::Image1 = &image1;
	EdgeCostSingleton::Image2 = &image2;
	EdgeCostSingleton::Margin = margin;
	EdgeCostSingleton::LargeNumber = 1000000.0 * gridWidth * gridHeight;

	// Run maxflow computation
	CutGrid grid(gridHeight, gridWidth);
	grid.setEdgeCostFunction(&EdgeCostSingleton::EdgeCost);
	grid.setSource(0, 0);
	grid.setSink(0, gridWidth - 1);
	grid.getMaxFlow();

	// Generate output
	int image1Offset = image1[0].size() - margin;
	for (int y = 0; y < gridHeight; ++y)
	{
		output.push_back(vector<float>(image1[0].size() + image2[0].size() - margin));
		vector<float>* vecRef = &output.data()[output.size() - 1];
		float* ptr = vecRef->data();

		// Copy over image 1
		for (int x = 0; x < image1Offset; ++x)
			ptr[x] = image1[y][x];

		// Copy over the margin between the images
		for (int x = image1Offset; x < image1[0].size(); ++x)
			if (grid.getLabel(y, x - image1Offset) == CutPlanar::LABEL_SOURCE)
				ptr[x] = image1[y][x];
			else
				ptr[x] = image2[y][x - image1Offset];

		// Copy over image 2
		for (int x = 0; x < image2[0].size() - margin; ++x)
			ptr[x + image1[0].size()] = image2[y][x + margin];
	}
}

// Convert an 8-bit image array to a matrix of floats
void convertImageDataToFloatMatrix(const vector<unsigned char>& image, 
	const int width, const int height, vector<vector<float> >& output)
{
	for (int y = 0; y < height; ++y)
	{
		output.push_back(vector<float>(width));
		vector<float>* vecRef = &output.data()[output.size() - 1];
		float* ptr = vecRef->data();

		for (int x = 0; x < width; ++x)
			ptr[x] = (float)image[4 * (x + y * width)] / 255.0f;
	}
}

// Convert a matrix of float to an 8-bit image array
void convertFloatMatrixToImageData(const vector<vector<float> >& matrix,
	vector<unsigned char>& output)
{
	output.resize(4 * matrix[0].size() * matrix.size());
	unsigned char* dataPtr = output.data();
	int index = 0;
	for (const vector<float>& vec : matrix)
		for (float val : vec)
		{
			dataPtr[index++] = ((unsigned char)(val * 255.0f));
			dataPtr[index++] = ((unsigned char)(val * 255.0f));
			dataPtr[index++] = ((unsigned char)(val * 255.0f));
			dataPtr[index++] = 255;
		}
}

int main()
{
	// Open the PNG files for image 1 and image 2
	vector<unsigned char> imageData1;
	unsigned int imageWidth1;
	unsigned int imageHeight1;
	unsigned int error1 = lodepng::decode(imageData1, imageWidth1, imageHeight1, IMAGE_SOURCE_1);

	vector<unsigned char> imageData2;
	unsigned int imageWidth2;
	unsigned int imageHeight2;
	unsigned int error2 = lodepng::decode(imageData2, imageWidth2, imageHeight2, IMAGE_SOURCE_2);

	if (error1 || error2)
	{
		cout << "Failed to open image files!" << endl;
		return -1;
	}

	// Convert the image data to float matrices
	vector<vector<float> > imageArray1;
	vector<vector<float> > imageArray2;
	convertImageDataToFloatMatrix(imageData1, imageWidth1, imageHeight1, imageArray1);
	convertImageDataToFloatMatrix(imageData2, imageWidth2, imageHeight2, imageArray2);

	cout << "Stitching images..." << endl;

	// Stitch the images together
	vector<vector<float> > output;
	performStitching(imageArray1, imageArray2, STITCH_MARGIN, output);

	cout << "Stitching complete!" << endl;
	cout << "Saving result..." << endl;

	// Convert the result to an 8-bit image array
	vector<unsigned char> outputImageData;
	convertFloatMatrixToImageData(output, outputImageData);

	// Save the resulting image
	unsigned int error = lodepng::encode(IMAGE_OUTPUT, outputImageData, output[0].size(), output.size());

	if (error)
	{
		cout << "Failed to save result!" << endl;
		return -1;
	}

	cout << "Success!" << endl;

	return 0;
}

const vector<vector<float> >* EdgeCostSingleton::Image1;
const vector<vector<float> >* EdgeCostSingleton::Image2;
int EdgeCostSingleton::Margin;
double EdgeCostSingleton::LargeNumber;