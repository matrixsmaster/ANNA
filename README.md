# ANNA - Automatic Neural Network Assistant

ANNA is a very powerful cross-platform AI toolkit. It contains versatile CLI/GUI LLM tools optimized for local inference, and supports optional remote offloading.

## Lightweight Scripted Consciousness Simulator (LSCS)

LSCS is a powerful new addition to ANNA, which allows anyone to create complex multi-model load-sharing structures. These structures can include generation models, filtering models, multi-modal models, and any types of other user-supplied scripted blocks. Together, these blocks can represent a constantly running self-driving "consciousness" model, able to be aware of changes in its environment (virtual or physical) and react to them, while also being able to communicate with its user (if needed) and store/recall any data it processed previously. Blocks can physically run on different devices, allowing for an extremely efficient distributed parallel systems. Ultimately, LSCS allows anyone to create very complex and realistic "consciousness" simulations!

## ANNA Server

ANNA server is quite unique, as it allows you to host a multi-mod**a**l, multi-mod**e**l server without any constraint on the number of users. It uses preemptive type of multitask scheduling, allowing many users to share even relatively weak hardware quite efficiently. Every user can use their own unique models at the same time, no restrictions on how many different models are used simultaneously!

**ANNA Server is optional, and is not required for using either CLI or GUI interfaces (each of them has embedded inference engine).**

## ANNA CLI

Supported command line arguments:
* `quit()` - **gracefully quit from the program** - using this instead of hitting Ctrl-C is **HIGHLY** recommended in order to avoid all sorts of issues
* `-m` `<model_file>` - mandatory argument, loads the model file
* `-s` `<seed_value>` - sets seed value for internal RNG used for sampling
* `-t` `<number_of_threads>` - sets the number of CPU threads to be used
* `-p` `<prompt_file>` - loads a text file with prompt, can be used multiple times to define a "multi-prompt"
* `-f` `<token_enforcement_string>` - sets a string which becomes mandatory prefix for each of the model's replies
* `-c` `<cache_file>` - loads a model state cache file (if exists) or creates a new one after evaluating the prompt (for future faster start-ups with the same prompt)
* `-n` `<number_of_tokens_to_generate>` - limits the number of tokens to be generated by model on each reply
* `-e` `<temperature>` - sets the sampling temperature
* `-u` `<user_input_prefix>` - sets a string which then prepends eachs of user's input strings before being tokenized
* `-x` `<context_length>` - sets a context length (default is 4096 tokens)
* `-r` `<request_prefix> <request_suffix> <request_command>` - registers a "requester plugin" (see below), can be used multiple times to define multiple requesters
* `-v` - verbose flag, prints information about the model, the prompt(s) and so on
* `-T` `<terminator string>` - sets a termination marker string (if this string is generated by the model, ANNA exits)
* `-P` - enables Linux pipeline mode of operation (honors EOF in stdin, allowing to use ANNA in pipeline with other text-processing software)
* `-S` - enables use of EOS/BOS tokens, relying on them to split the model's responses
* `-N` - ignores newlines as potential speech separators, allowing the model to output multiple lines of text in one batch
* `-G` `<layers_to_offload_to_GPU>`
* `-F` `<ai/user>` - first turn flag (who speaks first), either "ai" or "user"
* `-M` - use mirostat-X sampling, where X is the mirostat version number (1 or 2)
* `-V` - vision projector file for image embeddings (in gguf format)
* `-i` - image file input (considered a secondary prompt)
* `-R` `<server_URL>` - use remote offloading onto ANNA server; automatically allows using `*.dummy` files


### Internal commands

Whenever system is ready to ingest user’s input, a user can use one of the following commands instead of actual text input:
* `load_file()` - prompts you to input a file name, then loads the whole file as user's input
* `no_input()` - allows the model to continue generating uninterrupted, no further user's input is possible
* `save()` - saves a full snapshot of the model to a user-specified file. Such a snapshot could then be loaded with the `-c` command line argument.
* `load()` - loads a previously saved full snapshot
* `add_user()` - adds a new user prefix. Note: this will make first user prefix to disappear, as ANNA would not be able to determine which user alias to use next, therefore entering the alias (prefix) will become user's job.
* `image()` - injects image embedding from a user-supplied file into the current context. A vision projector file must be specified during launch before using this command.

## Requester plugins

Requesters are external processes spawned by ANNA whenever it detects a particular template in the model's output.

For example, if a model has been instructed to do web searches using `<websearch>` tag, a requester can be registered which will be looking for that tag and executing an external command using the body of the tag as an argument. Whatever requester plugin outputs to stdin, is then fed back to the model.

This simple, yet powerful plugin interface allows models to execute arbitrary actions in the "real world", as well as reading and analyzing the results of their execution. You can use it to allow your model to search the web, create files, run Python code, or even execute `rm -rf /` if you’re not careful. With great power comes great responsibility!

### Requester example
Assume a model, which was prompted to use `<python>` tag to specify a block of executable Python code. Now, you can register a requester plugin like this: `anna -r "<python>" "</python>" "python3 -IBc " ...`  In this case, the python interpreter itself becomes a "plugin". Whatever text the model has outputted between `<python>` and `</python>` tags will be fed into your Python interpreter, and consequently whatever the interpter will print to stdout will be transferred back to the model as input text.

A more elaborate example might be in a situation where you actually construct an external plugin. Assume a model, which was prompted to use `<websearch>` tag to conduct a search on the Internet whenever needed. Assume as well that there's a shell script called `mysearch.sh` which performes the actual search, using its first argument as the search string. A requester registration then would look like `anna -r "<websearch>" "</websearch>" "mysearch.sh" ...`
You can combine multiple requesters with differentt tags. There's no need for the tags to look like XML tags. In fact, you can use any piece of text as prefix and suffix.

## ANNA GUI

The GUI version can be built with QtCreator. Qt version required is Qt 5.15. Open `anna_graphica/anna_graphica.pro` file in QtCreator, configure it for Release build, and click Build.

On Windows, the build is fully automated and done by qmake. On Linux, a prerequisite for GUI build is to build the main library first (do `make` in the root of the source tree).

The GUI contains built-in offline help system (WIP atm).


# AUTHORS

(C) Georgi Gerganov, 2023-2024

(C) llama.cpp contributors, 2023-2024

(C) Dmitry 'sciloaf' Solovyev, 2023-2025

(C) Syntheva AS, 2024-2025
