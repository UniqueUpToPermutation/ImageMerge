#include <iostream>
#include <fstream>
#include <vector>
#include <assert.h>
#include <string>
#include <algorithm>

#include "lodepng.h"
#include "CutGrid.h"

#define DEFAULT_IMAGE_SOURCE_1 "goat2.png"
#define DEFAULT_IMAGE_SOURCE_2 "cat.png"
#define DEFAULT_IMAGE_OUTPUT "result.png"
#define DEFAULT_STITCH_MARGIN 100
#define DEFAULT_MODE GradientStitch

using namespace std;

// Program mode
enum ProgramMode
{
	SimpleStitch,
	ComputeGradient,
	GradientStitch
};

// 2-component vector
template <typename t>
struct vec2
{
	t x, y;
};

template <typename t>
inline t magnitudeSquared(vec2<t> vec)
{
	return vec.x * vec.x + vec.y * vec.y;
}

template <typename t>
inline vec2<t> difference(vec2<t> a, vec2<t> b)
{
	return vec2<t> { a.x - b.x, a.y - b.y };
}

// Singleton class for computing the edge weights on the image grid
class EdgeCostSingleton
{
public:
	static struct DataObject
	{
		const vector<vector<float> >* Image1;
		const vector<vector<float> >* Image2;
		int Margin;
		double LargeNumber;
	} Data;

	static CapType EdgeCost(int row, int col, CutGrid::EDir dir) {
		// Edge weights on either side should be infinite
		if (col == 0 && (dir == CutGrid::DIR_NORTH || dir == CutGrid::DIR_SOUTH))
			return Data.LargeNumber;
		else if (col == Data.Margin - 1 && (dir == CutGrid::DIR_NORTH || dir == CutGrid::DIR_SOUTH))
			return Data.LargeNumber;

		// Otherwise, take the sum of the absolute differences between the pixels
		int dx = 0;
		int dy = 0;
		size_t image1Offset = (*Data.Image1)[0].size() - Data.Margin;

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
		weight += std::fabs((*Data.Image1)[row][image1Offset + col] - (*Data.Image2)[row2][col2]);
		weight += std::fabs((*Data.Image1)[row2][image1Offset + col2] - (*Data.Image2)[row][col]);

		return weight;
	}
};

// Singleton data definition
EdgeCostSingleton::DataObject EdgeCostSingleton::Data;

// Singleton class for computing the edge weights on the image grid for vector fields
class GradientEdgeCostSingleton
{
public:
	static struct DataObject
	{
		const vector<vector<vec2<float> > >* Field1;
		const vector<vector<vec2<float> > >* Field2;
		int Margin;
		double LargeNumber;
	} Data;

	static CapType EdgeCost(int row, int col, CutGrid::EDir dir) {
		// Edge weights on either side should be infinite
		if (col == 0 && (dir == CutGrid::DIR_NORTH || dir == CutGrid::DIR_SOUTH))
			return Data.LargeNumber;
		else if (col == Data.Margin - 1 && (dir == CutGrid::DIR_NORTH || dir == CutGrid::DIR_SOUTH))
			return Data.LargeNumber;

		// Otherwise, take the sum of the differences between the vectors
		int dx = 0;
		int dy = 0;
		size_t image1Offset = (*Data.Field1)[0].size() - Data.Margin;

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
		weight += magnitudeSquared(difference((*Data.Field1)[row][image1Offset + col], (*Data.Field2)[row2][col2]));
		weight += magnitudeSquared(difference((*Data.Field1)[row2][image1Offset + col2], (*Data.Field2)[row][col]));
		return weight;
	}
};

// Singleton data definition
GradientEdgeCostSingleton::DataObject GradientEdgeCostSingleton::Data;

// Stitch two images together using basic min-cut method
void performStitching(const vector<vector<float> >& image1,
	const vector<vector<float> >& image2,
	const int margin, vector<vector<float> >* output)
{
	int gridWidth = margin;
	int gridHeight = static_cast<int>(image1.size());

	// Prepare the singleton edge weight class
	EdgeCostSingleton::Data.Image1 = &image1;
	EdgeCostSingleton::Data.Image2 = &image2;
	EdgeCostSingleton::Data.Margin = margin;
	EdgeCostSingleton::Data.LargeNumber = 1000000.0 * gridWidth * gridHeight;

	// Run maxflow computation
	CutGrid grid(gridHeight, gridWidth);
	grid.setEdgeCostFunction(&EdgeCostSingleton::EdgeCost);
	grid.setSource(0, 0);
	grid.setSink(0, gridWidth - 1);
	grid.getMaxFlow();

	// Generate output
	int image1Offset = static_cast<int>(image1[0].size()) - margin;
	for (int y = 0; y < gridHeight; ++y)
	{
		output->push_back(vector<float>(image1[0].size() + image2[0].size() - margin));
		vector<float>& row = (*output)[output->size() - 1];

		// Copy over image 1
		for (int x = 0; x < image1Offset; ++x)
			row[x] = image1[y][x];

		// Copy over the margin between the images
		for (unsigned int x = image1Offset; x < image1[0].size(); ++x)
			if (grid.getLabel(y, x - image1Offset) == CutPlanar::LABEL_SOURCE)
				row[x] = image1[y][x];
			else
				row[x] = image2[y][x - image1Offset];

		// Copy over image 2
		for (unsigned int x = 0; x < image2[0].size() - margin; ++x)
			row[x + image1[0].size()] = image2[y][x + margin];
	}
}

void performGradientStitching(const vector<vector<vec2<float> > >& field1, const vector<vector<vec2<float> > >& field2,
	const int margin, vector<vector<vec2<float> > >* output)
{
	int gridWidth = margin;
	int gridHeight = static_cast<int>(field1.size());

	// Prepare the singleton edge weight class
	GradientEdgeCostSingleton::Data.Field1 = &field1;
	GradientEdgeCostSingleton::Data.Field2 = &field2;
	GradientEdgeCostSingleton::Data.Margin = margin;
	GradientEdgeCostSingleton::Data.LargeNumber = 1000000.0 * gridWidth * gridHeight;

	// Run maxflow computation
	CutGrid grid(gridHeight, gridWidth);
	grid.setEdgeCostFunction(&GradientEdgeCostSingleton::EdgeCost);
	grid.setSource(0, 0);
	grid.setSink(0, gridWidth - 1);
	grid.getMaxFlow();

	// Generate output
	int image1Offset = static_cast<int>(field1[0].size()) - margin;
	for (int y = 0; y < gridHeight; ++y)
	{
		output->push_back(vector<vec2<float> >(field1[0].size() + field2[0].size() - margin));
		vector<vec2<float> >& row = (*output)[output->size() - 1];

		// Copy over image 1
		for (int x = 0; x < image1Offset; ++x)
			row[x] = field1[y][x];

		// Copy over the margin between the images
		for (unsigned int x = image1Offset; x < field1[0].size(); ++x)
			if (grid.getLabel(y, x - image1Offset) == CutPlanar::LABEL_SOURCE)
				row[x] = field1[y][x];
			else
				row[x] = field2[y][x - image1Offset];

		// Copy over image 2
		for (unsigned int x = 0; x < field2[0].size() - margin; ++x)
			row[x + field1[0].size()] = field2[y][x + margin];
	}
}

// Compute a gradient from a scalar field
void computeGradient(const vector<vector<float> >& scalarField, vector<vector<vec2<float> > >* output)
{
	// Correct dimensions
	output->resize(scalarField.size());
	for (auto& row : *output)
		row.resize(scalarField[0].size());

	// Compute gradient
	for (int y = 0, ymax = static_cast<int>(scalarField.size()); y < ymax; ++y)
		for (int x = 0, xmax = static_cast<int>(scalarField[0].size()); x < xmax; ++x)
		{
			int sampleMinusX = std::max(0, x - 1);
			int samplePlusX = std::min(xmax - 1, x + 1);
			int sampleMinusY = std::max(0, y - 1);
			int samplePlusY = std::min(ymax - 1, y + 1);
			(*output)[y][x].x = (scalarField[y][samplePlusX] - scalarField[y][sampleMinusX]) / 2.0f;
			(*output)[y][x].y = (scalarField[samplePlusY][x] - scalarField[sampleMinusY][x]) / 2.0f;
		}
}

// Convert gradient data to image data
void convertGradientToImageData(vector<vector<vec2<float> > >& grad, vector<unsigned char>* output)
{
	output->resize(4 * grad.size() * grad[0].size());
	int index = 0;
	for (auto& row : grad)
	{
		for (auto val : row)
		{
			(*output)[index++] = ((unsigned char)(std::fmax(0.0, val.x) * 255.0f));
			(*output)[index++] = ((unsigned char)(std::fmax(0.0, val.y) * 255.0f));
			(*output)[index++] = 0;
			(*output)[index++] = 255;
		}
	}
}

// Convert an 8-bit image array to a matrix of floats
void convertImageDataToFloatMatrix(const vector<unsigned char>& image,
	const int width, const int height, vector<vector<float> >* output)
{
	for (int y = 0; y < height; ++y)
	{
		output->push_back(vector<float>(width));
		vector<float>& row = (*output)[output->size() - 1];

		for (int x = 0; x < width; ++x)
			row[x] = (float)image[4 * (x + y * width)] / 255.0f;
	}
}

// Convert a matrix of float to an 8-bit image array
void convertFloatMatrixToImageData(const vector<vector<float> >& matrix,
	vector<unsigned char>* output)
{
	output->resize(4 * matrix[0].size() * matrix.size());
	int index = 0;
	for (const vector<float>& vec : matrix)
		for (float val : vec)
		{
			(*output)[index++] = ((unsigned char)(val * 255.0f));
			(*output)[index++] = ((unsigned char)(val * 255.0f));
			(*output)[index++] = ((unsigned char)(val * 255.0f));
			(*output)[index++] = 255;
		}
}

unsigned int floatMatrixFromPNG(const string& filename, vector<vector<float> >* output)
{
	// Open the raw PNG data
	vector<unsigned char> imageData;
	unsigned int imageWidth;
	unsigned int imageHeight;
	unsigned int error = lodepng::decode(imageData, imageWidth, imageHeight, filename);

	if (error)
		return error;

	// Convert data to a float matrix
	convertImageDataToFloatMatrix(imageData, imageWidth, imageHeight, output);
	return 0;
}

unsigned int saveFloatMatrixToPNG(const string& filename, const vector<vector<float> >& data)
{
	vector<unsigned char> outputImageData;
	convertFloatMatrixToImageData(data, &outputImageData);

	// Save the resulting image
	return lodepng::encode(filename, outputImageData, static_cast<unsigned int>(data[0].size()),
		static_cast<unsigned int>(data.size()));
}

int main(int argc, char** argv)
{
	// Read command line inputs if specified
	string imageSource1 = DEFAULT_IMAGE_SOURCE_1;
	string imageSource2 = DEFAULT_IMAGE_SOURCE_2;
	string outputPath = DEFAULT_IMAGE_OUTPUT;
	int stitchMargin = DEFAULT_STITCH_MARGIN;
	ProgramMode mode = DEFAULT_MODE;

	// Read input parameters if needed
	if (argc >= 2)
	{
		imageSource1 = argv[0];
		imageSource2 = argv[1];
	}
	if (argc >= 3)
		stitchMargin = stoi(argv[2]);
	if (argc >= 4)
		outputPath = argv[3];
	if (argc >= 5)
		mode = static_cast<ProgramMode>(stoi(argv[4]));

	// Open the PNG files for image 1 and image 2
	vector<vector<float> > imageArray1;
	vector<vector<float> > imageArray2;
	unsigned int error1 = floatMatrixFromPNG(imageSource1, &imageArray1);
	unsigned int error2 = floatMatrixFromPNG(imageSource2, &imageArray2);

	if (error1 || error2)
	{
		cout << "Failed to open image files!" << endl;
		return -1;
	}

	unsigned int error;
	switch (mode)
	{
	case SimpleStitch:
	{
		// Stitch the images together
		vector<vector<float> > output;
		cout << "Stitching images..." << endl;
		performStitching(imageArray1, imageArray2, stitchMargin, &output);
		cout << "Stitching complete!" << endl;

		// Save the result
		cout << "Saving result..." << endl;
		error = saveFloatMatrixToPNG(outputPath, output);
		break;
	}
	case ComputeGradient:
	{
		// Compute the gradient of an image and save the result
		vector<vector<vec2<float> > > gradient;
		computeGradient(imageArray1, &gradient);
		vector<unsigned char> outputData;
		convertGradientToImageData(gradient, &outputData);
		error = lodepng::encode(outputPath, outputData, static_cast<unsigned int>(gradient[0].size()),
			static_cast<unsigned int>(gradient.size()));
		break;
	}
	case GradientStitch:
	{
		// Stitch the gradients together
		vector<vector<vec2<float> > > gradient1;
		vector<vector<vec2<float> > > gradient2;
		computeGradient(imageArray1, &gradient1);
		computeGradient(imageArray2, &gradient2);

		vector<vector<vec2<float> > > gradientOutput;
		cout << "Stitching gradients..." << endl;
		performGradientStitching(gradient1, gradient2, stitchMargin, &gradientOutput);
		cout << "Stitching complete!" << endl;

		// Save the result
		cout << "Saving result..." << endl;
		vector<unsigned char> outputData;
		convertGradientToImageData(gradientOutput, &outputData);
		error = lodepng::encode(outputPath, outputData, static_cast<unsigned int>(gradientOutput[0].size()),
			static_cast<unsigned int>(gradientOutput.size()));
		break;
	}
	}

	if (error)
	{
		cout << "Failed to save result!" << endl;
		return -1;
	}

	cout << "Success!" << endl;
	return 0;
}