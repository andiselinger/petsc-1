'''
A Template is a default mechanism for constructing a BuildGraph. They are similar in spirit to
the default Make rules. Users with simple needs can use them to automatically build a project.
Any template should have a getTarget() call which provides a BuildGraph.
'''
import base
import build.buildGraph
import build.compile.SIDL

class Template(base.Base):
  '''This template constructs BuildGraphs capable of compiling SIDL into server and client source'''
  def __init__(self, sourceDB, project, dependenceGraph, usingSIDL = None):
    base.Base.__init__(self)
    self.sourceDB        = sourceDB
    self.project         = project
    self.dependenceGraph = dependenceGraph
    self.usingSIDL       = usingSIDL
    if self.usingSIDL is None:
      import build.templates.usingSIDL
      self.usingSIDL = build.templates.usingSIDL.UsingSIDL(self.sourceDB, self.project)
    return

  def addServer(self, lang):
    return self.usingSIDL.addServer(lang)

  def addClient(self, lang):
    return self.usingSIDL.addClient(lang)

  def addRepositoryDirs(self, compiler):
    compiler.repositoryDirs.extend([vertex.getRoot() for vertex in build.buildGraph.BuildGraph.depthFirstVisit(self.dependenceGraph, self.project) if not vertex == self.project])
    return compiler

  def getServerTarget(self):
    '''Return a BuildGraph which will compile SIDL into the clients specified'''
    import build.bk
    import os

    rootDir  = self.project.getRoot()
    rootFunc = lambda f: os.path.join(rootDir, self.usingSIDL.getServerRootDir(lang, os.path.splitext(os.path.basename(f))[0]))
    target   = build.buildGraph.BuildGraph()
    for lang in self.usingSIDL.serverLanguages:
      lastVertex = None
      vertex     = build.bk.Tag(rootFunc = rootFunc, inputTag = ['sidl', 'old sidl'])
      target.addEdges(lastVertex, outputs = [vertex])
      lastVertex = vertex
      vertex     = build.bk.Open()
      target.addEdges(lastVertex, outputs = [vertex])
      lastVertex = vertex
      vertex     = self.addRepositoryDirs(build.compile.SIDL.Compiler(self.sourceDB, lang, rootDir, 1, self.usingSIDL))
      target.addEdges(lastVertex, outputs = [vertex])
      lastVertex = vertex
      vertex     = build.bk.Tag(rootFunc = rootFunc, inputTag = ['update sidl', 'old sidl'])
      target.addEdges(lastVertex, outputs = [vertex])
      lastVertex = vertex
      vertex     = build.bk.Close()
      target.addEdges(lastVertex, outputs = [vertex])
    return target

  def getClientTarget(self):
    '''Return a BuildGraph which will compile SIDL into the clients specified
       - Currently this graph is just a list of unconnected vertices, which will be linked up in the final target'''
    target = build.buildGraph.BuildGraph()
    for lang in self.usingSIDL.clientLanguages:
      #TODO: Static packages should be linked with the server by the linker
      target.addVertex(self.addRepositoryDirs(build.compile.SIDL.Compiler(self.sourceDB, lang, self.project.getRoot(), 0, self.usingSIDL)))
    return target

  def getTarget(self):
    '''Return a BuildGraph which will compile SIDL into the servers and clients specified'''
    import build.fileState

    target = build.buildGraph.BuildGraph()
    tagger = build.fileState.GenericTag(self.sourceDB, 'sidl', ext = 'sidl')
    target.addVertex(tagger)
    client = self.getClientTarget()
    server = self.getServerTarget()
    target.addSubgraph(client)
    target.addSubgraph(server)
    target.addEdges(tagger, outputs = build.buildGraph.BuildGraph.getRoots(client)+build.buildGraph.BuildGraph.getRoots(server))
    target.appendGraph(build.buildGraph.BuildGraph([build.fileState.Update(self.sourceDB)]))
    return target
