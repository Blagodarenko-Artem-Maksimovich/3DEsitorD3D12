#include "BasicUtil.h"

std::wstring BasicUtil::getCroppedName(WCHAR* filename)
{
	//making pretty name
	std::wstringstream ss(filename);
	std::vector<std::wstring> chunks;
	std::wstring chunk;

	while (std::getline(ss, chunk, L'\\'))
	{
		chunks.push_back(chunk);
	}

	std::wstring name = chunks[chunks.size() - 1];
	return name.substr(0, name.size() - 4);
}

std::string BasicUtil::trimName(const std::string& name, int border)
{
	return name.size() > border ? name.substr(0, 12) + "..." : name;
}

bool BasicUtil::TryToOpenFile(WCHAR* extension1, WCHAR* extension2, PWSTR& filePath)
{
	IFileOpenDialog* pFileOpen;

	// Create the FileOpenDialog object.
	ThrowIfFailed(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen)));

	COMDLG_FILTERSPEC rgSpec[] = { extension1, extension2 };

	//filter only for .obj files
	pFileOpen->SetFileTypes(1, rgSpec);

	// Show the Open dialog box.
	HRESULT hr = pFileOpen->Show(NULL);
	if (FAILED(hr))
	{
		if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
		{
			// User closed the dialog manually, just return safely
			return false;
		}
		else
		{
			// Handle other errors
			ThrowIfFailed(hr);
		}
	}

	// Get the file name from the dialog box.
	IShellItem* pItem;
	ThrowIfFailed(pFileOpen->GetResult(&pItem));
	PWSTR pszFilePath;
	ThrowIfFailed(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath));

	filePath = pszFilePath;

	pItem->Release();
	pFileOpen->Release();

	return true;
}