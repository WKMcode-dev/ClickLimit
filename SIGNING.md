# Assinatura e distribuicao

Esta versao NAO esta assinada. O icone, os metadados de versao e o hash nao sao uma certificacao de seguranca.

"Windows protegeu o computador" e um aviso do Microsoft Defender SmartScreen, que pode aparecer para aplicativos novos ou sem reputacao. Uma assinatura Authenticode confiavel identifica o autor e protege a integridade, mas nao garante ausencia de avisos, deteccoes ou bloqueios. Certificados autoassinados nao estabelecem reputacao publica. Certificados EV tambem nao garantem reputacao imediata.

Para distribuicao publica, obtenha uma identidade de assinatura validada por uma autoridade reconhecida ou pelo servico Microsoft Artifact Signing, se elegivel. Isso exige cadastro/validacao e pode ter custos; nada foi contratado, comprado ou configurado neste projeto. Outra opcao e distribuicao por MSIX na Microsoft Store, sujeita as regras e certificacao da Store.

## Fluxo de release

1. Compile em Release e execute os testes isolados.
2. Assine o executavel final com SHA-256 e carimbo de tempo. O script `tools/Sign-Release.ps1` atende certificados de assinatura de codigo instalados no armazenamento pessoal do Windows. Ele nao gera certificados nem exporta chaves. Servicos em nuvem exigem seu proprio cliente de assinatura.
3. Verifique a assinatura. Exemplo: `Get-AuthenticodeSignature .\ClickLimit.exe` deve retornar `Valid`.
4. Gere o ZIP e o hash somente APOS assinar. Qualquer modificacao posterior no executavel invalida a assinatura.
5. Publique com identidade consistente entre releases para construir reputacao. Nao anuncie que assinatura garante aprovacao do Windows.

Se houver deteccao de uma ameaca especifica pelo Defender, conserve o nome da deteccao e encaminhe o arquivo para analise da Microsoft. Isso e diferente do aviso de reputacao do SmartScreen. Nao e necessario desativar o antivirus ou criar exclusoes para desenvolver este projeto.

Fontes oficiais:
- https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
- https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options
- https://www.microsoft.com/en-us/wdsi/filesubmission

## Preparacao automatizada (sem alterar o autoclick)

Os scripts de distribuicao nao modificam motor, interface ou configuracoes do Windows.
A release 0.8.0 existente continua sem assinatura; este preparo nao transforma seus downloads antigos em downloads assinados.

1. Obtenha um certificado publico de assinatura de codigo com chave privada acessivel em Cert:\CurrentUser\My. Nao envie chaves, PFX ou senhas para o repositorio.
2. Compile e teste o aplicativo. Prepare uma pasta nova de distribuicao, fora da pasta de codigo, com os mesmos arquivos da release.
3. Assine TODOS os EXE/DLL incluidos, inclusive ClickLimitBenchmark.exe se ele fizer parte do pacote:
   `./tools/Sign-Release.ps1 -Executable '<arquivo.exe>' -CertificateThumbprint '<thumbprint>' -SignToolPath '<signtool.exe>'`
4. Gere o ZIP final:
   `./tools/Package-SignedRelease.ps1 -PackageDirectory '<pasta ClickLimit>' -OutputZip '<pasta de saida>/ClickLimit-signed.zip' -CertificateThumbprint '<thumbprint>' -SignToolPath '<signtool.exe>'`
5. Publique ZIP, arquivo .sha256 e arquivo .signatures.json juntos. O empacotamento recusa binarios sem assinatura valida, sem timestamp ou com identidade diferente. Nao sobrescreve saidas existentes.
6. Nao modifique os arquivos durante a verificacao/empacotamento nem depois da assinatura.

O formato atual e portatil: nao exige um instalador novo nem privilegios administrativos.
Se futuramente houver instalador, ele tambem deve ser assinado e verificado apos ser gerado.
Para Microsoft Artifact Signing, use o cliente oficial de assinatura antes do passo 4; nao coloque credenciais no codigo. O script Sign-Release atende certificado local, nao configura conta de nuvem.

O workflow Validate distribution compila em um runner Windows do GitHub e executa somente os testes de motor/layout e benchmark SIMULADO, sem enviar cliques reais. Tambem verifica sintaxe dos scripts e rejeicao de pacote nao assinado. Ele nao publica nem assina releases automaticamente.

A assinatura ainda depende do certificado real do autor. SmartScreen pode continuar exibindo avisos de reputacao; nao existe garantia de zero avisos. Nao desative protecoes nem crie exclusoes como parte da instalacao.

Referencia tecnica: https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool
