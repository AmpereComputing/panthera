# Jupyter

Jupyter is an interactive python environment which can display rich notebooks
for data-analysis using python which can be useful for analyzing model results.

## Examples

### Pandas

```python
import pandas as pd
from scipy.stats import gmean

workloads = pd.read_csv("my-study/rollup.by_workload.csv").set_index([
    "configuration",
    "workload",
    "suite",
]).rename(columns=lambda c: c.replace("sys.soc_0.core_0.", ""))

speedup = workloads.loc[:, 'ipc'] / workloads.loc['base', 'ipc']
```

Geomean IPC speedup:
```python
speedup.groupby(level='configuration').agg(gmean)
```

IPC speedup S-curve:
```python
speedup.unstack(level='configuration').reset_index(drop=True).apply(lambda c: c.sort_values(ignore_index=True), axis=0).plot()
```

## Jupytext

It is recommended to *not* commit Jupyter notebooks (`*.ipynb` extension) into
the repository since they are unreadable and don't allow us to perform diffs
easily. Instead use [jupytext](https://jupytext.readthedocs.io/en/latest).

### Create a Jupytext file

Create a Jupytext file through the Jupyter/JupyterLab interface provided by
your installation, for example by selecting a Python percent-format notebook
in its launcher. The exact menu names depend on the installed Jupytext and
Jupyter versions.

### Opening a py jupytext file

You can directly open and edit a Jupytext Python file from the Jupyter file
browser, normally with `Open With -> Jupytext Notebook`. The cached output is
not saved in the Jupytext file itself.

### Linking a jupytext file with a ipynb file

By linking a Jupytext file (`*.py`) to a Jupyter notebook (`.ipynb`), cached
output can be saved for later viewing.

To link a Jupytext file, open it as described above and select `File ->
Jupytext -> Pair notebook with ipynb document`. After the first save, the
Jupytext and notebook files stay in sync.

If you want to automatically pair all your jupytext files with a jupyter file format file you can place the following config file at `$HOME/.config/jupytext/jupytext.toml`

```shell
# Pair ipynb notebooks to py:percent text notebooks
formats = "ipynb,py:percent"
```

### Convert existing ipynb file to jupytext percent format

You can use jupytext utility to convert an existing ipynb file to the jupytext format.

    nix-shell -p python311Packages.jupytext --run 'jupytext --to py:percent input.ipynb'

## Packages/Modules

* `panthera.*` : a collection of python modules for rollup/reporting/etc.

## Resources

* JupyterLab [documentation](https://docs.jupyter.org/en/latest/)
* Pandas [documentation](https://pandas.pydata.org/)
* Bokeh [documentation](https://docs.bokeh.org/en/latest/)
* Holoviews [documentation](https://holoviews.org/)
* Panel [documentation](https://panel.holoviz.org/index.html)
