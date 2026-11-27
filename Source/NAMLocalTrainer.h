#pragma once

#include <JuceHeader.h>

namespace NAMLocalTrainer
{
inline juce::File runtimeDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("NAMplified").getChildFile ("Trainer-Python-20261110");
}

inline juce::File environment()
{
    return runtimeDirectory().getChildFile ("venv");
}

inline juce::File bundledArchive()
{
   #if JUCE_MAC && JUCE_ARM
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
        .getParentDirectory().getParentDirectory().getChildFile ("Resources/NAMTrainerPython-20261110.tar.gz");
   #else
    return {};
   #endif
}

inline juce::File baseInterpreter()
{
    return runtimeDirectory().getChildFile ("python/bin/python3.11");
}

inline juce::File interpreter()
{
    return environment().getChildFile ("bin/python");
}

inline constexpr auto setupCode = R"py(
import os, subprocess, sys
from pathlib import Path
folder, log = sys.argv[1:]
with open(log, "w") as stream:
    os.dup2(stream.fileno(), 1)
    os.dup2(stream.fileno(), 2)
    import venv
    import tkinter
    venv.EnvBuilder(with_pip=True).create(folder)
    python = os.path.join(folder, "bin", "python")
    subprocess.check_call([python, "-I", "-u", "-m", "pip", "--disable-pip-version-check",
                           "install", "neural-amp-modeler==0.12.0", "torch==2.5.1",
                           "torchaudio==2.5.1", "transformers==4.57.6", "requests>=2.32,<3"])
    subprocess.check_call([python, "-I", "-c", "from nam.train.core import train"])
    (Path(folder) / "ready").write_text("0.12.0", encoding="utf-8")
)py";

inline constexpr auto trainingCode = R"py(
import os, sys
from pathlib import Path
folder = Path(sys.argv[1])
with (folder / "training.log").open("w") as stream:
    os.dup2(stream.fileno(), 1)
    os.dup2(stream.fileno(), 2)
    os.environ["MPLBACKEND"] = "Agg"
    import traceback
    try:
        from nam.train.core import train
        print("Starting NAM training and checking alignment...", flush=True)
        result = train(str(folder / "input.wav"), str(folder / "output.wav"),
                       str(folder), silent=True, local=True)
        if result is None or result.model is None:
            raise RuntimeError("NAM rejected this recording. See the training log.")
        result.model.net.export(folder, basename="Capture-in-progress",
                                other_metadata={"training": result.metadata.model_dump()})
        (folder / "Capture-in-progress.nam").replace(folder / "Capture.nam")
        print("Training complete.", flush=True)
    except Exception:
        traceback.print_exc()
        sys.exit(1)
)py";
}
